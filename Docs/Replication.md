# Network Replication

A backend-agnostic replication layer for `CWorld`. The transport (sockets) is pluggable —
GameNetworkingSockets (GNS) is the primary backend, with an in-process loopback driver for PIE.
State replication is built on the existing reflection serialization (`CStruct::SerializeTaggedProperties`,
`FProperty::Serialize`, `FMemoryWriter`/`FMemoryReader`) and entity serialization
(`ECS::Utils::SerializeRegistry` / `SerializeEntity`).

Dirty detection is **push-only**: gameplay code calls `Rep::MarkPropertyDirty` (or `self:MarkDirty`
in Lua) when it mutates a replicated property. There is **no** shadow-copy / `FProperty::Identical`
comparison on the replication path — a property that is mutated without being marked simply will not
replicate. This is the contract.

The role scaffolding already exists: `ENetMode { Standalone, Client, ListenServer, DedicatedServer }`
on `FWorldContext` (`World/WorldContext.h`), threaded through `FWorldManager::CreateWorldContext` /
`StartPIE`, and reachable from any system via `CWorld::GetNetMode()`.

## Three layers, strictly separated

Each layer knows only about the one below it. The replication layer never includes a GNS header.

```
Game layer        PROPERTY(Replicated), FUNCTION(Server/Client/NetMulticast),
                  Rep::MarkPropertyDirty                         ← backend-blind
Replication       NetGUIDs, FRepLayout, dirty masks, snapshot
                  deltas, channels, relevancy, RPC dispatch      ← transport-blind
Transport         INetDriver / INetConnection                    ← only layer that knows GNS
                  FGNSNetDriver | FLoopbackNetDriver
```

## Layer 1 — Transport (`Runtime/Networking/Transport/`)

A small channel-oriented seam. GNS, loopback, and any future UDP/ENet backend all express the same
two primitives.

```cpp
enum class ENetChannel : uint8 { ReliableOrdered, UnreliableSequenced };

class INetConnection
{
public:
    virtual void   Send(ENetChannel, const uint8* Data, uint32 Size) = 0;
    virtual void   Flush() = 0;                       // GNS: FlushMessagesOnConnection
    virtual void   Close() = 0;
    virtual bool   IsValid() const = 0;
    virtual uint32 GetMaxPacketSize() const = 0;
    virtual void   GetQuality(float& OutRttMs, float& OutLoss) const = 0;
    FNetConnectionId Id;
};

class INetDriver
{
public:
    virtual bool InitListen(uint16 Port) = 0;                              // server
    virtual bool InitConnect(const FStringView& Addr, uint16 Port) = 0;    // client
    virtual void Tick() = 0;                                               // pump transport
    virtual void Shutdown() = 0;

    TMulticastDelegate<INetConnection*>             OnConnected;
    TMulticastDelegate<INetConnection*>             OnDisconnected;
    TMulticastDelegate<INetConnection*, FNetPacket> OnReceive;
};
```

Backends (the only files that change per transport):

- **`FGNSNetDriver`** — `ISteamNetworkingSockets`, `CreateListenSocketIP` / `ConnectByIPAddress`,
  poll groups, `SendMessageToConnection` with `k_nSteamNetworkingSend_Reliable` vs
  `_UnreliableNoNagle`. GNS supplies fragmentation, reliability, RTT/loss — the two `ENetChannel`s
  map onto its send flags directly. Registered in `ThirdParty.lua`; includes propagate via
  `ModuleDependencies` (only the `Networking` module depends on it).
- **`FLoopbackNetDriver`** — in-process, zero sockets. Backs PIE "Listen Server + N Clients" by
  letting multiple `CWorld`s in one process talk over a queue. The everyday iteration path and the
  determinism harness.

Drivers are name-registered so `CGameInstance` selects via config:
`GNetDriverRegistry.Register("GNS", ...)`, `Register("Loopback", ...)`.

## Layer 2 — Replication

### Roles & authority

`ENetMode` is the *world's* role. Each replicated entity also carries a *per-entity* role:

```cpp
enum class ENetRole : uint8 { None, SimulatedProxy, AutonomousProxy, Authority };

struct SReplicatedComponent          // identity + policy; only on replicated entities
{
    FNetGUID         NetGUID;
    ENetRole         LocalRole = ENetRole::Authority;
    FNetConnectionId Owner;           // owning connection (Server RPC / autonomous source)
    uint8            NetUpdateFrequency = 30;
    bool             bAlwaysRelevant = false;
    bool             bNetStartup = false;   // static (level-authored) — see NetGUIDs
};
```

Authority lives on the server; clients hold proxies. Systems branch on `ENetMode`/`ENetRole` the
same way existing systems branch on `EWorldType` (`ScriptSystem.cpp`, `CameraSystem.cpp`).

### NetGUIDs — dynamic vs static (the net-startup efficiency)

Two id spaces in `FNetGUIDCache` (per driver, maps `FNetGUID ↔ entt::entity` both ways):

- **Dynamic** — assigned by the server when an entity is *spawned during play*. Requires a reliable
  **spawn message** (prefab/archetype ref + initial full state); clients learn the mapping from it.
- **Static (net-startup)** — for entities authored into the level and present at load. Server and
  client load the **same map deterministically**, so each placed entity is assigned a NetGUID
  derived from a **stable, load-order-independent key** (hash of its persistent serialized
  identity within the level package — the same stable identity `SerializeEntity` already keys on),
  *not* its volatile `entt::entity` handle. **No spawn message is sent.**

Payoff on join: the server says nothing about the thousands of authored props/lights/geometry — the
client already has them from the map. For static entities the server sends only:
1. a **delta** for any whose replicated state changed from the authored baseline before the client
   joined (a door opened pre-join), and
2. a reliable **destroy** for any destroyed before the client joined (a crate already blown up).

Unreferenced static entities cost zero join bandwidth.

### Property replication — push-model dirty marking

For each component `CStruct`, the reflector precomputes a **`FRepLayout`** once per type: the flat
list of `FProperty*` carrying `Replicated` metadata, each assigned a stable **local bit index**
(0..N within that component). No property-count cap — dirty masks are variable-width
**`FRepDirtyMask`** (a word-backed dynamic bit array, `TVector<uint64>` chunks sized to the
layout's property count; the engine has fixed-`N` `TBitSet`/`eastl::bitset` and `FBitSetAllocator`,
but no dynamic bit array, so `FRepDirtyMask` is new). Width is fixed once at layout build, so the
mark path is a single word OR — no reallocation.

```cpp
struct SHealthComponent
{
    PROPERTY(Replicated) float Health;     // local rep bit 0
    PROPERTY(Replicated) float Armor;      // local rep bit 1
    PROPERTY()           float Regen;       // not replicated
};
```

**Marking dirty (C++)** — type-safe via member pointer; offset → local bit is an O(1) lookup
generated alongside the `FRepLayout`:

```cpp
Rep::MarkPropertyDirty(World, Entity, &SHealthComponent::Health);
// or the generated index constant:
Rep::MarkPropertyDirty(World, Entity, SHealthComponent::ERep::Health);
// convenience macro:
MARK_REP_DIRTY(World, Entity, SHealthComponent, Health);
```

**Marking dirty (Lua)** — string-resolved (no member pointers in Lua). Two cases:

```lua
self:MarkDirty("Health")                          -- a replicated script Export field (see "Lua script replication")
Rep.MarkDirty(entity, "STransformComponent", "Location")  -- a replicated C++ component field, by reflected name
```

(Optional convenience, default off: auto-mark when a `Replicated` C++ property is *written* through
the Lua binding setter, since that path already goes through reflection. Kept opt-in so the explicit
model stays the norm.)

**Dirty state storage** — a `SReplicationState` component on replicated entities, holding per
replicated-component masks (small inline map `FComponentTypeID → FRepDirty`):

```cpp
struct FRepDirty
{
    FRepDirtyMask Incremental;   // OR'd by MarkPropertyDirty; captured + cleared each server net tick
    FRepDirtyMask SinceBaseline; // static entities only; monotonic; used to seed new connections
};
```

`MarkPropertyDirty` ORs the local bit into both masks. No registry scan, no comparison.

**Delta serialization** — wire form per entity:
`[NetGUID][component id][changed bitmask][changed property values...]`, each value via
`FProperty::Serialize(Ar, GetValuePtr(Comp))`. Unchanged properties cost one bit; unchanged
entities cost nothing.

### Snapshots, acks, channels, and loss recovery (still no comparison)

- **UnreliableSequenced** carries the per-tick snapshot delta, tagged with a sequence number. Loss
  is tolerated by design (next snapshot re-deltas from the still-acked baseline).
- **ReliableOrdered** carries what must arrive exactly once: spawn messages, entity destroys,
  static-destroy notifications, reliable RPCs, join handshake.

Per (entity), a small **changelist history ring** `{ Seq, Mask }`. Per (connection, entity):
`LastAckedSeq` and an `bInitialReplicated` flag. Each server net tick, per relevant connection:

```
if (!bInitialReplicated)
    SendMask = dynamic ? (full state via spawn msg) : Dirty.SinceBaseline   // seed newcomer
else
    SendMask = OR of History[Seq].Mask for all Seq > LastAckedSeq           // incl. loss recovery
```

After capture: push `{ Seq, Dirty.Incremental }` to the ring, clear `Incremental`. On `ack(S)`:
`LastAckedSeq = S`. Prune ring entries `< min(LastAckedSeq)` across relevant connections. A static
entity never marked dirty has `SinceBaseline == 0` → a late joiner gets nothing (its authored copy
already matches). Every send mask is built purely from `MarkPropertyDirty` history — never a value
comparison.

**Dev-only dirty audit (compiled out in Shipping):** in Debug/Dev, optionally keep a shadow copy and
`assert`/warn when a replicated property changed without being marked. Catches missing
`MarkPropertyDirty` calls during development without putting any comparison on the shipping path.

### Relevancy

`bAlwaysRelevant` entities (local player, global game state) always replicate. Everything else runs
a relevancy filter — distance-based first (transforms are available), frustum/visibility later.
Irrelevant entities are absent from a connection's snapshot.

### RPCs via FUNCTION reflection

The reflector already parses inline `FUNCTION` macros. Annotate direction + reliability:

```cpp
FUNCTION(Server, Reliable)   void RequestFire(FVector Target);   // client → authority
FUNCTION(Client, Unreliable) void PlayHitFx(FVector At);         // authority → owning client
FUNCTION(NetMulticast)       void OnExplode();                   // authority → all relevant
```

Dispatch serializes `[NetGUID][function id][params]`, picks the channel from the metadata, and
validates call direction against the entity's `ENetRole`. On receive: resolve NetGUID → entity,
deserialize params into a reflected stack frame, invoke.

## Lua script replication

Lua script state lives in the script's Lua table, not in reflected `CStruct` memory, so the
`FRepLayout`/`FProperty` path does not cover it. But scripts already declare **typed** state via the
`Exports` schema (`FScriptExportSchema`, harvested at load in `BuildSchemaFromExportsTable`,
`ScriptExports.cpp`), and those values already serialize with the world via the self-describing
`FScriptPropertyValue` tagged union. Lua replication rides that: **replicated Lua state is a marked
subset of `Exports`**, reusing the existing value codec and slotting into the *same* per-connection
delta / changelist-history / ack machinery as C++ components — only the value (de)serialization
differs.

### Declaring replicated state

Alongside `Exports`, a script declares which fields replicate. Harvested at load with the same
`lua_getfield` pass that reads `Exports`:

```lua
type Exports = { Health: number, Ammo: number, DisplayName: string }

-- replicated subset (+ optional per-field config)
local NetReplicated = {
    Health      = { cond = "All" },          -- to every relevant connection
    Ammo        = { cond = "OwnerOnly" },    -- only to the owning connection (v1: All | OwnerOnly)
    -- DisplayName not listed -> not replicated
}
```

At load this builds a per-**script-type** `FScriptRepLayout`: the replicated `Exports` fields sorted
by name (deterministic, identical on server and client since both run the same bytecode), each given
a stable local bit index. It is cached with the module/bytecode like the `Exports` schema is — *not*
per instance. The per-instance dirty mask lives in `SReplicationState` under a reserved synthetic
`FComponentTypeID` ("ScriptExports"), so the snapshot codec, history ring, ack, and relevancy logic
are unchanged; only the serialize step reads/writes the live Lua table instead of component memory.

### Marking dirty (push-only, same contract)

```lua
self.Health = self.Health - 10
self:MarkDirty("Health")          -- explicit; resolves "Health" -> FScriptRepLayout bit
```

`self:MarkDirty(name)` resolves the name against the script's `FScriptRepLayout` and ORs the bit into
the entity's `FRepDirty` (both `Incremental` and, for net-startup scripted entities, `SinceBaseline`).
No comparison, consistent with the C++ path. (`Exports` values are stored as plain table fields, not
behind a metatable, so there is no write hook to auto-mark from — explicit marking is the natural fit.
Auto-mark would require routing `Exports` through a `__newindex` proxy; deferred.)

### Serializing across the boundary (game thread only)

All Lua reads/writes happen on the **game thread** (single shared `lua_State`); the net thread only
moves bytes. In `SNetServerSystem` (FrameEnd) the dirty replicated fields are read from the live
`Exports` table (via the script's `FRef`) and written as `FScriptPropertyValue` into the snapshot
buffer. In `SNetClientSystem` (FrameStart) the delta is read back and applied into the proxy's
`Exports` table, then RepNotify callbacks fire.

### RepNotify & net lifecycle callbacks

Discovered at attach as `FRef`s exactly like `OnUpdate` (invalid `FRef` = not defined), invoked by
the net systems:

- `OnRep_<Field>(oldValue)` — per-field, fired on the client after a replicated `Exports` field is
  applied (e.g. `function self:OnRep_Health(old) ... end`). A generic `OnReplicated(name, old)`
  fallback covers fields without a named handler.
- `OnNetSpawn()` — client, when the entity is first replicated (dynamic spawn or first relevance).
- `OnNetDestroy()` — client, before a replicated entity is removed.

### Lua RPCs

Declared in a `Net` table for direction + reliability; the named function is the receive-side
handler. Calls are explicit (matching the explicit philosophy):

```lua
local Net = {
    Fire    = { dir = "Server",    reliable = true  },   -- client -> authority
    PlayFx  = { dir = "Client",    reliable = false },   -- authority -> owning client
    Explode = { dir = "Multicast", reliable = true  },   -- authority -> all relevant
}

function self:Fire(target)  ... end                       -- runs on the authority
self:ServerRPC("Fire", targetEntity)                      -- call from a client
self:MulticastRPC("Explode")                              -- call from the authority
self:ClientRPC("PlayFx", hitPos)                          -- authority -> owner
```

Params marshal through the existing `TStack` specializations (number/bool/string/`glm::vec*`/
`entt::entity`). **Entity params serialize as `FNetGUID`** over the wire and resolve back via
`FNetGUIDCache` on receive (queued if the referenced entity hasn't replicated yet). Direction +
reliability come from the `Net` entry; the engine validates the call against the entity's `ENetRole`
and routes to the matching `ENetChannel`. Dispatch reuses the C++ RPC path — only param marshalling
goes through the Lua stack instead of a reflected frame.

### Role / authority queries

Exposed on `self` and `World` so scripts can branch (`OnUpdate` runs on server and clients alike):

```lua
if self:IsAuthority() then ... end       -- ENetRole == Authority
if self:IsLocallyOwned() then ... end    -- this connection owns the entity (autonomous proxy)
self:GetNetRole()                        -- ENetRole
World:GetNetMode()                       -- ENetMode; World:IsServer()/IsClient() helpers
```

### What is not replicated

Only marked `Exports` fields and declared `Net` RPCs. Local Lua variables and unmarked `Exports`
follow the existing rule (not serialized / not replicated). Scripts wanting replicated or persistent
state must put it in `Exports`.

## Threading & update-stage integration

Replication rides the existing game/physics/render split. All registry access stays on the **game
thread** — the net thread only handles opaque byte buffers (avoids the live-registry race that the
render-thread extract pattern already guards against).

- **`FNetThread`** (mirrors `FPhysicsThread`, CVar-gated) — owns `INetDriver::Tick()` (socket pump),
  may run a frame behind. Inbound packets land in a concurrent queue drained at `FrameStart`;
  serialized outbound snapshots are handed off at `FrameEnd`.
- **`SNetClientSystem`** — `EUpdateStage::FrameStart`. Drains inbound, applies deltas to proxies,
  processes spawn/destroy, runs interpolation/reconciliation. Gated to client `ENetMode`.
- **`SNetServerSystem`** — `EUpdateStage::FrameEnd`. After gameplay/physics settle, walks relevant
  replicated entities, builds per-connection deltas from dirty history, hands buffers to
  `FNetThread`. Gated to `ListenServer`/`DedicatedServer`.

Ordering: client applies authority state at `FrameStart`, prediction/interpolation runs mid-frame,
render `Extract` snapshots the result — no new cross-thread plumbing.

## Session ownership — `CGameInstance`

The "Phase 2" `CGameInstance` (already referenced by `FWorldContext::GameInstance`) outlives
individual worlds and owns the session: the active `INetDriver`, the connection list, the
`FNetGUIDCache`, and open/connect/travel flow. A `CWorld` borrows the driver for the session's
duration.

## Work breakdown

### Runtime (`Runtime/Networking/`, new module)
1. **Transport** — `INetDriver`/`INetConnection`, `FNetConnectionId`, `FNetPacket`, channel enum,
   `GNetDriverRegistry`. `FLoopbackNetDriver` (in-proc queues).
2. **`FGNSNetDriver`** — GNS vendored in `ThirdParty.lua`; module depends on it. Connection wraps
   `HSteamNetConnection`; channels → send flags; `GetQuality` from GNS status.
3. **NetGUID** — `FNetGUID`, `FNetGUIDCache` (dynamic counter + static stable-key assignment;
   pending-reference queue for as-yet-unreplicated NetGUIDs).
4. **`FRepLayout`** + **`FRepDirtyMask`** — per-`CStruct` cache of replicated `FProperty*` + local
   bit indices + member-offset→bit table, built lazily on first use, keyed off `Replicated`
   metadata; `FRepDirtyMask` is a word-backed (`TVector<uint64>`) variable-width bit array sized
   from the layout (new container — engine has only fixed-`N` `TBitSet`).
5. **Components** — `SReplicatedComponent`, `SReplicationState` (`FRepDirty` map). Connect
   `on_construct<SReplicatedComponent>` to register NetGUID + initial rep state.
6. **Dirty API** — `Rep::MarkPropertyDirty` (member-ptr + generated-index overloads), `MARK_REP_DIRTY`.
7. **Snapshot codec** — delta writer/reader over `FMemoryWriter`/`FMemoryReader`; changelist history
   ring; per-connection ack + `bInitialReplicated` tracking; prune-by-min-ack.
8. **Spawn/destroy** — reliable-channel spawn (prefab ref + full state) and destroy messages;
   static-destroy notifications.
9. **RPC dispatch** — `FUNCTION` net-metadata table; serialize/resolve/invoke; direction+reliability
   validation; Lua handler bridge.
10. **`FNetThread`** + `SNetServerSystem` / `SNetClientSystem` (update-stage + `ENetMode` gating).
11. **`CGameInstance`** — driver/connection/cache ownership; open/connect/travel.

### Reflector
12. Emit per-`Replicated`-property stable local bit index + offset→bit table into the component's
    generated header (consumed by `FRepLayout` and member-ptr `MarkPropertyDirty`). Parse
    `FUNCTION(Server|Client|NetMulticast, Reliable|Unreliable)` net metadata.

### Lua (`Runtime/Scripting/Lua/`)
13. **Replicated Exports** — harvest a `NetReplicated` declaration alongside `Exports` (extend the
    `lua_getfield` pass in `Scripting.cpp` / `ScriptExports.cpp`); build a per-script-type
    `FScriptRepLayout` (replicated fields sorted by name → stable local bits), cached with the module.
14. **Dirty + state slot** — `self:MarkDirty(name)`; reserved synthetic `FComponentTypeID`
    ("ScriptExports") in `SReplicationState`; server-side read of dirty `Exports` fields → snapshot via
    `FScriptPropertyValue`, client-side apply into the proxy `Exports` table (game thread only).
    Plus `Rep.MarkDirty(entity, "Component", "Property")` for C++ components by reflected name.
15. **RepNotify / lifecycle** — discover `OnRep_<Field>` / `OnReplicated` / `OnNetSpawn` /
    `OnNetDestroy` `FRef`s at attach (same pattern as `OnUpdate`); invoke from `SNetClientSystem`.
16. **Lua RPCs** — `Net` declaration table (dir + reliability); `self:ServerRPC` / `:ClientRPC` /
    `:MulticastRPC`; params via `TStack`, entity params serialized as `FNetGUID`; reuse the C++ RPC
    dispatch path.
17. **Role queries** — `self:IsAuthority` / `:IsLocallyOwned` / `:GetNetRole`,
    `World:GetNetMode` / `:IsServer` / `:IsClient`.

### Editor / Dev
18. PIE "Net Mode" picker (Standalone / Listen + N Clients / Dedicated + N Clients) using
    `FLoopbackNetDriver`; per-PIE-world net role badge.
19. Dev-only dirty audit (shadow + assert on unmarked change), compiled out in Shipping.
20. Net stats overlay (RTT/loss from `GetQuality`, bytes/s in/out, relevant-entity count).

### Build
- New `.cpp/.h` require `Tools/premake5.exe vs2022` before MSBuild (modules glob sources).
- GNS registered in `ThirdParty.lua`.

## Phasing

1. **Foundation** — module, transport interfaces, `FLoopbackNetDriver`, `FNetGUID`/cache,
   `FNetThread`. Echo bytes between two PIE worlds.
2. **Static replication** — net-startup NetGUIDs, join handshake, reliable static-destroy. Client
   joins to the authored world with zero spawn traffic.
3. **Property delta (push-model)** — `FRepLayout`, `MarkPropertyDirty`, dirty masks, changelist
   history, snapshot/ack on the unreliable channel. Replicate a moving transform. **Lua: replicated
   `Exports` + `self:MarkDirty` + `OnRep_*`** ride the same machinery here.
4. **Dynamic spawn/destroy + RPCs** — dynamic NetGUIDs, spawn messages, `FUNCTION` dispatch.
   **Lua: `Net` table + `Server`/`Client`/`Multicast` RPCs + role queries** land with this phase.
5. **GNS backend** — `FGNSNetDriver`. Everything above is unchanged — the proof the seam held.
6. **Polish** — relevancy/distance culling, client prediction + reconciliation (autonomous proxies),
   interpolation (simulated proxies), bandwidth throttling from GNS RTT/loss.

Loopback-first is deliberate: a fully working, deterministic, debuggable replicated game in-process
*before* sockets — adding GNS then debugs one new file, not the whole system.

## Deferred (out of scope for v1)
- Client-side prediction + server reconciliation (phase 6; v1 ships server-authoritative +
  interpolation only).
- Sub-object / nested-struct fine-grained dirty (v1 marks at the top-level property).
- Property rep conditions (`OwnerOnly`, `SkipOwner`, `InitialOnly`) beyond `bAlwaysRelevant`.
- Encryption / packet auth (GNS SDR relay covers transport hardening initially).
- Seamless map travel keeping connections open.
- Lua: replicating `Array` / `NestedStruct` `Exports` kinds (v1 covers scalar fields —
  bool/int/double/string/`vec*`); `__newindex`-proxy auto-mark for replicated `Exports`.
