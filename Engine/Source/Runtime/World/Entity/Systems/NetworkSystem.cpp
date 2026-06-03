#include "pch.h"
#include "NetworkSystem.h"

#include <algorithm>
#include "SystemContext.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "World/WorldManager.h"
#include "World/Net/NetWorldState.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectCore.h"
#include "World/Net/NetRpc.h"
#include "World/Net/NetReplication.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "World/Entity/EntityHandle.h"
#include "Networking/NetworkGlobals.h"
#include "Networking/INetworkTransport.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsTypes.h"
#include "Core/Serialization/NetArchive.h"
#include "Core/Serialization/NetQuantize.h"
#include "Core/Console/ConsoleVariable.h"
#include "Log/Log.h"
#include "EASTL/sort.h"

namespace Lumina
{
    // Periodic full transform resync. Transforms are state, so re-sending every replicated pose on this
    // cadence lets a dropped delta self-heal without per-packet acks. 0 means deltas only.
    static TConsoleVar<float> CVarTransformKeyframeInterval("Net.Transform.KeyframeInterval", 0.5f,
        "Seconds between full transform keyframes (movement resync so dropped deltas self-heal). 0 = off.");

    namespace
    {
        // Loopback port for in-editor listen-server PIE. CVar-ize when real connections land.
        constexpr uint16 NetDefaultPort = 7777;

        bool IsServerMode(ENetMode Mode)
        {
            return Mode == ENetMode::ListenServer || Mode == ENetMode::DedicatedServer;
        }

        const char* NetModeToString(ENetMode Mode)
        {
            switch (Mode)
            {
            case ENetMode::Standalone:      return "Standalone";
            case ENetMode::Client:          return "Client";
            case ENetMode::ListenServer:    return "ListenServer";
            case ENetMode::DedicatedServer: return "DedicatedServer";
            }
            return "Unknown";
        }

        // Pre-placed (level) entities exist on every peer after they load the same world. We assign each
        // a stable NetGUID deterministically (sorted by entity id, which deserialization recreates
        // identically on all peers), so references match across the wire with no spawn message.
        void AdoptStableNetworkEntities(entt::registry& Registry, FNetWorldState& State, bool bAuthority)
        {
            TVector<entt::entity> Networked;
            for (entt::entity Entity : Registry.view<SNetworkComponent>())
            {
                Networked.push_back(Entity);
            }

            eastl::sort(Networked.begin(), Networked.end(), [](entt::entity A, entt::entity B)
            {
                return entt::to_integral(A) < entt::to_integral(B);
            });

            uint32 StableId = 1; // stable range is [1, NetGUID_DynamicStart)
            for (entt::entity Entity : Networked)
            {
                SNetworkComponent& Net = Registry.get<SNetworkComponent>(Entity);
                if (!Net.bNetLoadOnClient)
                {
                    continue; // server-only, never gets a network identity
                }
                Net.NetGUID            = FNetGUID{ StableId++ };
                Net.OwningConnectionId = 0; // unowned until the server assigns it (SetOwner)
                State.GuidTable.Register(Net.NetGUID, Entity);

                // Server tracks live stable ids so a level entity destroyed at runtime can be despawned to
                // clients (which would otherwise keep their level-derived copy).
                if (bAuthority)
                {
                    State.KnownStableGuids.push_back(Net.NetGUID.Value);
                }
            }

            LOG_DISPLAY("[Net] Adopted {} stable networked entities ({})", Networked.size(), bAuthority ? "authority" : "proxy");
        }

        // Derive each entity's roles from net mode + ownership. Server is Authority for all; a client is
        // AutonomousProxy for entities it owns and SimulatedProxy for the rest. Run every tick.
        void RefreshNetRoles(entt::registry& Registry, const FNetWorldState& State, bool bServer)
        {
            for (entt::entity Entity : Registry.view<SNetworkComponent>())
            {
                SNetworkComponent& Net = Registry.get<SNetworkComponent>(Entity);
                if (bServer)
                {
                    Net.LocalRole  = ENetRole::Authority;
                    Net.RemoteRole = (Net.OwningConnectionId != 0) ? ENetRole::AutonomousProxy : ENetRole::SimulatedProxy;
                }
                else
                {
                    const bool bOwnedHere = (Net.OwningConnectionId != 0) && (Net.OwningConnectionId == State.LocalPeerId);
                    Net.LocalRole  = bOwnedHere ? ENetRole::AutonomousProxy : ENetRole::SimulatedProxy;
                    Net.RemoteRole = ENetRole::Authority;
                }
            }
        }

        // Server to clients, full ownership table. Reliable since ownership is state-defining. Sent whenever
        // it changes or a client joins.
        void BroadcastOwnership(entt::registry& Registry, TVector<uint8>& Batch)
        {
            auto View = Registry.view<SNetworkComponent>();
            uint16 Count = 0;
            for (entt::entity Entity : View)
            {
                if (View.get<SNetworkComponent>(Entity).bNetLoadOnClient) { ++Count; }
            }
            if (Count == 0)
            {
                return;
            }

            TVector<uint8> Buffer;
            FNetArchive Writer(Buffer);
            uint8 Type = static_cast<uint8>(ENetMessage::OwnershipUpdate);
            Writer << Type;
            Writer << Count;
            for (entt::entity Entity : View)
            {
                SNetworkComponent& Net = View.get<SNetworkComponent>(Entity);
                if (!Net.bNetLoadOnClient) { continue; } // server-only, not replicated
                uint32 Guid  = Net.NetGUID.Value;
                uint32 Owner = Net.OwningConnectionId;
                Writer << Guid;
                Writer << Owner;
            }
            Net::AppendFramedMessage(Batch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));
        }

        // Client, apply an ownership table. Roles refresh from it on the same tick.
        void ApplyOwnershipUpdate(entt::registry& Registry, FNetWorldState& State, const uint8* Data, SIZE_T Size)
        {
            FNetArchive Reader(Data, Size);
            uint8  Type  = 0;
            uint16 Count = 0;
            Reader << Type;
            Reader << Count;
            for (uint16 i = 0; i < Count; ++i)
            {
                uint32 Guid = 0, Owner = 0;
                Reader << Guid;
                Reader << Owner;
                if (Reader.HasError())
                {
                    break;
                }
                const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
                if (Entity != entt::null && Registry.valid(Entity))
                {
                    if (SNetworkComponent* Net = Registry.try_get<SNetworkComponent>(Entity))
                    {
                        Net->OwningConnectionId = Owner;
                    }
                }
            }
        }

        bool VectorContains(const TVector<uint32>& V, uint32 Value)
        {
            for (uint32 X : V) { if (X == Value) { return true; } }
            return false;
        }

        // Net-index cache helpers live in the Net namespace (NetReplication.cpp) so the RPC path can share
        // them. See the package-map notes there.

        // Serialize one SpawnEntity message.
        void WriteSpawnMessage(entt::registry& Registry, FNetWorldState& State, entt::entity Entity, uint32 Guid, uint32 Owner, TVector<uint8>& OutMsg)
        {
            FNetArchive Writer(OutMsg);
            Net::BindWriters(Writer, State);
            uint8 Type = static_cast<uint8>(ENetMessage::SpawnEntity);
            Writer << Type;
            Writer << Guid;
            Writer << Owner;
            Net::WriteEntityComponents(Writer, Registry, Entity);
        }
        
        void BuildExistingSpawnBatch(entt::registry& Registry, FNetWorldState& State, TVector<uint8>& OutBatch)
        {
            for (entt::entity Entity : Registry.view<SNetworkComponent>())
            {
                SNetworkComponent& Net = Registry.get<SNetworkComponent>(Entity);
                if (Net.NetGUID.Value < NetGUID_DynamicStart || !Net.bReplicates || !Net.bNetLoadOnClient)
                {
                    continue;
                }
                TVector<uint8> Msg;
                WriteSpawnMessage(Registry, State, Entity, Net.NetGUID.Value, Net.OwningConnectionId, Msg);
                Net::AppendFramedMessage(OutBatch, Msg.data(), static_cast<SIZE_T>(Msg.size()));
            }
        }
        
        void ReplicateSpawns(entt::registry& Registry, FNetWorldState& State, TVector<uint8>& Batch)
        {
            auto View = Registry.view<SNetworkComponent>();

            for (entt::entity Entity : View)
            {
                SNetworkComponent& Net = View.get<SNetworkComponent>(Entity);
                if (!Net.bNetLoadOnClient)
                {
                    continue; // server-only, never replicated to clients
                }
                if (Net.NetGUID.Value == 0)
                {
                    Net.NetGUID = State.GuidTable.AllocateDynamic();
                    State.GuidTable.Register(Net.NetGUID, Entity);
                }
            }

            TVector<uint32> Live;
            for (entt::entity Entity : View)
            {
                SNetworkComponent& Net = View.get<SNetworkComponent>(Entity);
                if (Net.NetGUID.Value < NetGUID_DynamicStart || !Net.bReplicates || !Net.bNetLoadOnClient)
                {
                    continue;
                }
                Live.push_back(Net.NetGUID.Value);
                if (VectorContains(State.KnownSpawnedGuids, Net.NetGUID.Value))
                {
                    continue;
                }

                TVector<uint8> Buffer;
                WriteSpawnMessage(Registry, State, Entity, Net.NetGUID.Value, Net.OwningConnectionId, Buffer);
                Net::AppendFramedMessage(Batch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));
                State.KnownSpawnedGuids.push_back(Net.NetGUID.Value);
            }

            for (size_t i = 0; i < State.KnownSpawnedGuids.size(); )
            {
                const uint32 Guid = State.KnownSpawnedGuids[i];
                if (!VectorContains(Live, Guid))
                {
                    TVector<uint8> Buffer;
                    FNetArchive Writer(Buffer);
                    uint8 Type = static_cast<uint8>(ENetMessage::DespawnEntity);
                    Writer << Type;
                    Writer << State.KnownSpawnedGuids[i];
                    Net::AppendFramedMessage(Batch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));
                    State.GuidTable.Unregister(FNetGUID{ Guid });
                    State.KnownSpawnedGuids.erase(State.KnownSpawnedGuids.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
        }
        
        void ReplicateStableDespawns(entt::registry& Registry, FNetWorldState& State, TVector<uint8>& Batch)
        {
            for (size_t i = 0; i < State.KnownStableGuids.size(); )
            {
                const uint32 Guid = State.KnownStableGuids[i];
                const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
                if (Entity == entt::null || !Registry.valid(Entity))
                {
                    TVector<uint8> Buffer;
                    FNetArchive Writer(Buffer);
                    uint8  Type = static_cast<uint8>(ENetMessage::DespawnEntity);
                    uint32 G    = Guid;
                    Writer << Type;
                    Writer << G;
                    Net::AppendFramedMessage(Batch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));

                    State.GuidTable.Unregister(FNetGUID{ Guid });
                    State.DestroyedStableGuids.push_back(Guid);
                    State.KnownStableGuids.erase(State.KnownStableGuids.begin() + i);
                }
                else
                {
                    ++i;
                }
            }
        }

        // Server, reliable property delta for every entity flagged FNetDirty. Sends the entity's replicated
        // components, reliability handles resend, then clears the tag.
        void ReplicateDirtyProperties(entt::registry& Registry, FNetWorldState& State, TVector<uint8>& Batch)
        {
            auto View = Registry.view<SNetworkComponent, FNetDirty>();
            for (entt::entity Entity : View)
            {
                SNetworkComponent& Net = View.get<SNetworkComponent>(Entity);
                if (Net.bReplicates && Net.NetGUID.Value != 0)
                {
                    TVector<uint8> Buffer;
                    FNetArchive Writer(Buffer);
                    Net::BindWriters(Writer, State);
                    uint8  Type = static_cast<uint8>(ENetMessage::PropertyUpdate);
                    uint32 Guid = Net.NetGUID.Value;
                    Writer << Type;
                    Writer << Guid;
                    Net::WriteEntityComponents(Writer, Registry, Entity);
                    Net::AppendFramedMessage(Batch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));
                }
            }
            Registry.clear<FNetDirty>();
        }
        
        void AttachReplicatedScript(CWorld* World, entt::registry& Registry, entt::entity Entity)
        {
            SScriptComponent* Comp = Registry.try_get<SScriptComponent>(Entity);
            if (Comp == nullptr)
            {
                return;
            }

            const FStringView Path = Comp->ScriptPath.ResolvePath();
            if (Path.empty())
            {
                return; // ref/export not resolvable yet
            }

            // Already running exactly this script, nothing to do.
            if (Comp->Script != nullptr &&
                FStringView(Comp->Script->Path.c_str(), Comp->Script->Path.size()) == Path)
            {
                return;
            }

            // Nothing attached and this exact path already failed, don't reload every tick.
            if (Comp->Script == nullptr)
            {
                if (FScriptAttachFailed* Failed = Registry.try_get<FScriptAttachFailed>(Entity))
                {
                    if (FStringView(Failed->Path.c_str(), Failed->Path.size()) == Path)
                    {
                        return;
                    }
                }
            }

            // (Re)load from the now-current ScriptPath. Detaches the old script first if the server swapped it.
            World->ReloadScriptForComponent(Entity, *Comp);

            if (Comp->Script == nullptr)
            {
                Registry.emplace_or_replace<FScriptAttachFailed>(Entity).Path.assign(Path.data(), Path.size());
                LOG_WARN("[Net] Replicated script '{}' failed to load on this peer -- marking failed (won't reload until the path changes or the entity respawns).",
                    FString(Path.data(), Path.size()).c_str());
            }
            else
            {
                Registry.remove<FScriptAttachFailed>(Entity); // success clears any prior failure
            }
        }

        // Client, create + link a server-spawned entity, then apply its components.
        void ApplySpawnEntity(CWorld* World, entt::registry& Registry, FNetWorldState& State, uint32 SenderConn, const uint8* Data, SIZE_T Size)
        {
            FNetArchive Reader(Data, Size);
            uint8  Type = 0;
            uint32 Guid = 0, Owner = 0;
            Reader << Type;
            Reader << Guid;
            Reader << Owner;
            if (Reader.HasError() || State.GuidTable.Find(FNetGUID{ Guid }) != entt::null)
            {
                return; // malformed, or we already have this entity
            }

            Net::BindReaders(Reader, State, SenderConn);
            const entt::entity Entity = Registry.create();
            Net::ReadEntityComponents(Reader, Registry, Entity);

            SNetworkComponent& Net = Registry.get_or_emplace<SNetworkComponent>(Entity);
            Net.NetGUID            = FNetGUID{ Guid };
            Net.OwningConnectionId = Owner;
            State.GuidTable.Register(Net.NetGUID, Entity);
            Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);

            // A replicated script attaches here (path now set) so the entity gains its RPC handlers.
            AttachReplicatedScript(World, Registry, Entity);
        }

        // Client, apply a reliable property delta to an existing entity.
        void ApplyPropertyUpdate(CWorld* World, entt::registry& Registry, FNetWorldState& State, uint32 SenderConn, const uint8* Data, SIZE_T Size)
        {
            FNetArchive Reader(Data, Size);
            uint8  Type = 0;
            uint32 Guid = 0;
            Reader << Type;
            Reader << Guid;
            if (Reader.HasError())
            {
                return;
            }
            const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
            if (Entity != entt::null && Registry.valid(Entity))
            {
                Net::BindReaders(Reader, State, SenderConn);
                Net::ReadEntityComponents(Reader, Registry, Entity);
                Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
                AttachReplicatedScript(World, Registry, Entity);
            }
        }

        // Client, destroy a despawned entity.
        void ApplyDespawnEntity(CWorld* World, FNetWorldState& State, const uint8* Data, SIZE_T Size)
        {
            FNetArchive Reader(Data, Size);
            uint8  Type = 0;
            uint32 Guid = 0;
            Reader << Type;
            Reader << Guid;
            if (Reader.HasError())
            {
                return;
            }
            entt::registry& Registry = World->GetEntityRegistry();
            const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
            if (Entity != entt::null && Registry.valid(Entity))
            {
                State.GuidTable.Unregister(FNetGUID{ Guid });
                ECS::Utils::DestroyEntity(Registry, Entity);
            }
        }
        
        void ConfigureProxyPhysics(entt::registry& Registry, CWorld* World)
        {
            Physics::IPhysicsScene* PhysScene = World->GetPhysicsScene();
            if (PhysScene == nullptr)
            {
                return;
            }

            for (entt::entity Entity : Registry.view<SNetworkComponent>())
            {
                SNetworkComponent& Net = Registry.get<SNetworkComponent>(Entity);
                const bool bIsProxy = Net.LocalRole == ENetRole::SimulatedProxy || Net.LocalRole == ENetRole::AutonomousProxy;
                if (Net.bProxyPhysicsConfigured || !bIsProxy)
                {
                    continue;
                }

                const uint32 BodyID = PhysScene->GetEntityBodyID(Entity);
                if (BodyID != ~0u) // body created yet?
                {
                    PhysScene->ChangeBodyMotionType(BodyID, EBodyType::Kinematic);
                    Net.bProxyPhysicsConfigured = true;
                }
            }
        }

        // True once a local pose has moved past a small tolerance from the last-sent one (~1mm / tiny rotation).
        bool MovementChanged(const FVector3& OldPos, const FQuat& OldRot, const FVector3& NewPos, const FQuat& NewRot)
        {
            const FVector3 D = NewPos - OldPos;
            if (D.x * D.x + D.y * D.y + D.z * D.z > 1e-6f)
            {
                return true;
            }
            const float Dot = NewRot.x * OldRot.x + NewRot.y * OldRot.y + NewRot.z * OldRot.z + NewRot.w * OldRot.w;
            return 1.0f - (Dot < 0.0f ? -Dot : Dot) > 1e-6f;
        }
        
        void BroadcastTransformSnapshot(entt::registry& Registry, TVector<uint8>& Batch, bool bForceResend, float DeltaTime, float ServerTime)
        {
            auto&& TransformStorage = Registry.storage<STransformComponent>();
            auto View = Registry.view<SNetworkComponent>();

            // First pass: pick the entities to send (don't touch the cache until we commit to sending).
            TVector<entt::entity> ToSend;
            for (entt::entity Entity : View)
            {
                SNetworkComponent& Net = View.get<SNetworkComponent>(Entity);
                if (!Net.bReplicates || !Net.bReplicatesMovement || !Net.bNetLoadOnClient)
                {
                    continue;
                }

                Net.TimeSinceLastNetUpdate += DeltaTime;

                STransformComponent& Transform = TransformStorage.get(Entity);
                const FVector3 Pos = Transform.GetLocalLocation();
                const FQuat    Rot = Transform.GetLocalRotation();

                if (!bForceResend)
                {
                    if (Net.bMovementCacheValid && !MovementChanged(Net.LastSentLocation, Net.LastSentRotation, Pos, Rot))
                    {
                        continue; // unchanged since last send
                    }
                    
                    const float Interval = (Net.NetUpdateFrequency > 0.0f) ? (1.0f / Net.NetUpdateFrequency) : 0.0f;
                    if (Net.TimeSinceLastNetUpdate < Interval)
                    {
                        continue;
                    }
                }
                ToSend.push_back(Entity);
            }

            if (ToSend.empty())
            {
                return;
            }

            TVector<uint8> Buffer;
            FNetArchive Writer(Buffer);
            uint8 Type = static_cast<uint8>(ENetMessage::TransformSnapshot);
            Writer << Type;
            Writer << ServerTime;
            uint16 Count = static_cast<uint16>(ToSend.size());
            Writer << Count;

            for (entt::entity Entity : ToSend)
            {
                SNetworkComponent&   Net       = View.get<SNetworkComponent>(Entity);
                STransformComponent& Transform = TransformStorage.get(Entity);
                uint32   Guid = Net.NetGUID.Value;
                FVector3 Pos  = Transform.GetLocalLocation();
                FQuat    Rot  = Transform.GetLocalRotation();
                Writer << Guid;
                NetQuantize::WritePackedVector(Writer, Pos);
                NetQuantize::WritePackedQuat(Writer, Rot);

                Net.LastSentLocation       = Pos;
                Net.LastSentRotation       = Rot;
                Net.bMovementCacheValid    = true;
                Net.TimeSinceLastNetUpdate = 0.0f;
            }

            Net::AppendFramedMessage(Batch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));
        }

        // Client, apply each received pose directly to the entity its NetGUID maps to.
        void ApplyTransformSnapshot(entt::registry& Registry, FNetWorldState& State, const uint8* Data, SIZE_T Size)
        {
            FNetArchive Reader(Data, Size);
            uint8 Type = 0;
            Reader << Type; // ENetMessage::TransformSnapshot (already routed by the caller)
            float ServerTime = 0.0f;
            Reader << ServerTime;
            uint16 Count = 0;
            Reader << Count;

            const double SampleTime = static_cast<double>(ServerTime);
            State.LatestServerTime = std::max(SampleTime, State.LatestServerTime);

            for (uint16 Index = 0; Index < Count; ++Index)
            {
                uint32   Guid = 0;
                FVector3 Pos;
                FQuat    Rot;
                Reader << Guid;
                NetQuantize::ReadPackedVector(Reader, Pos);
                NetQuantize::ReadPackedQuat(Reader, Rot);
                if (Reader.HasError())
                {
                    break;
                }

                const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
                if (Entity == entt::null || !Registry.valid(Entity) || !Registry.all_of<STransformComponent>(Entity))
                {
                    continue;
                }
                // Skip entities we control. They're driven by our local input, so applying the stale server
                // pose would fight our own movement.
                if (SNetworkComponent* Net = Registry.try_get<SNetworkComponent>(Entity))
                {
                    if (Net->LocalRole == ENetRole::AutonomousProxy) { continue; }
                }

                // Buffer the sample; the per-frame interpolation step writes the smoothed pose. Don't snap
                // here, snapping makes movement step at the send rate.
                State.InterpStates[Guid].Push(SampleTime, Pos, Rot);
            }
        }

        // Client per-frame, advance the render clock and write each SimulatedProxy's interpolated pose.
        // Lerp position and rotation between bracketing samples.
        void InterpolateProxies(entt::registry& Registry, FNetWorldState& State, double ClientNow)
        {
            if (State.InterpStates.empty())
            {
                return;
            }

            // Track the server/client clock offset so RenderTime advances smoothly with the local frame clock
            // and stays ~InterpDelay behind the newest received server time. Gentle EMA absorbs jitter/drift.
            if (!State.bClockInitialized)
            {
                State.ClockOffset       = State.LatestServerTime - ClientNow;
                State.bClockInitialized = true;
            }
            else
            {
                const double Measured = State.LatestServerTime - ClientNow;
                State.ClockOffset += (Measured - State.ClockOffset) * 0.02;
            }

            const double RenderTime = ClientNow + State.ClockOffset - State.InterpDelay;

            for (auto It = State.InterpStates.begin(); It != State.InterpStates.end(); )
            {
                const uint32     Guid   = It->first;
                FNetInterpState& Buffer = It->second;

                const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
                if (Buffer.Count == 0 || Entity == entt::null || !Registry.valid(Entity) || !Registry.all_of<STransformComponent>(Entity))
                {
                    It = State.InterpStates.erase(It); // entity gone, drop its buffer
                    continue;
                }

                // The owner's own entity is driven locally, not interpolated.
                if (SNetworkComponent* Net = Registry.try_get<SNetworkComponent>(Entity))
                {
                    if (Net->LocalRole == ENetRole::AutonomousProxy) { ++It; continue; }
                }

                FVector3 Pos;
                FQuat    Rot;
                Buffer.Evaluate(RenderTime, Pos, Rot);

                STransformComponent& Transform = Registry.get<STransformComponent>(Entity);
                FTransform NewTransform;
                NewTransform.Location = Pos;
                NewTransform.Rotation = Rot;
                NewTransform.Scale    = Transform.GetLocalScale();
                Transform.SetLocalTransform(NewTransform);
                Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
                ++It;
            }
        }
        
        void SendOwnedTransforms(entt::registry& Registry, FNetWorldState& State, float DeltaTime)
        {
            auto View = Registry.view<SNetworkComponent, STransformComponent>();
            TVector<entt::entity> ToSend;
            for (entt::entity Entity : View)
            {
                SNetworkComponent& Net = View.get<SNetworkComponent>(Entity);
                if (Net.LocalRole != ENetRole::AutonomousProxy || !Net.bReplicatesMovement)
                {
                    continue;
                }

                Net.TimeSinceLastNetUpdate += DeltaTime;

                STransformComponent& T = View.get<STransformComponent>(Entity);
                const FVector3 Pos = T.GetLocalLocation();
                const FQuat    Rot = T.GetLocalRotation();
                if (Net.bMovementCacheValid && !MovementChanged(Net.LastSentLocation, Net.LastSentRotation, Pos, Rot))
                {
                    continue;
                }
                // Throttle the upstream to the owner's NetUpdateFrequency (the server relays at its own rate).
                const float Interval = (Net.NetUpdateFrequency > 0.0f) ? (1.0f / Net.NetUpdateFrequency) : 0.0f;
                if (Net.TimeSinceLastNetUpdate < Interval)
                {
                    continue;
                }
                ToSend.push_back(Entity);
            }
            if (ToSend.empty())
            {
                return;
            }

            TVector<uint8> Buffer;
            FNetArchive Writer(Buffer);
            uint8  Type  = static_cast<uint8>(ENetMessage::ClientTransform);
            uint16 Count = static_cast<uint16>(ToSend.size());
            Writer << Type;
            Writer << Count;
            for (entt::entity Entity : ToSend)
            {
                SNetworkComponent&   Net = View.get<SNetworkComponent>(Entity);
                STransformComponent& T   = View.get<STransformComponent>(Entity);
                uint32   Guid = Net.NetGUID.Value;
                FVector3 Pos  = T.GetLocalLocation();
                FQuat    Rot  = T.GetLocalRotation();
                Writer << Guid;
                NetQuantize::WritePackedVector(Writer, Pos);
                NetQuantize::WritePackedQuat(Writer, Rot);
                Net.LastSentLocation       = Pos;
                Net.LastSentRotation       = Rot;
                Net.bMovementCacheValid    = true;
                Net.TimeSinceLastNetUpdate = 0.0f;
            }
            Net::SendFramed(*State.Transport, State.ServerConnection, Buffer.data(), static_cast<SIZE_T>(Buffer.size()), 0, ESendMode::UnreliableSequenced);
        }
        
        void ApplyClientTransform(entt::registry& Registry, FNetWorldState& State, FConnectionHandle Sender, double ServerNow, const uint8* Data, SIZE_T Size)
        {
            FNetArchive Reader(Data, Size);
            uint8  Type  = 0;
            uint16 Count = 0;
            Reader << Type;
            Reader << Count;

            State.LatestServerTime = std::max(ServerNow, State.LatestServerTime);

            for (uint16 i = 0; i < Count; ++i)
            {
                uint32   Guid = 0;
                FVector3 Pos;
                FQuat    Rot;
                Reader << Guid;
                NetQuantize::ReadPackedVector(Reader, Pos);
                NetQuantize::ReadPackedQuat(Reader, Rot);
                if (Reader.HasError())
                {
                    break;
                }

                const entt::entity Entity = State.GuidTable.Find(FNetGUID{ Guid });
                if (Entity == entt::null || !Registry.valid(Entity))
                {
                    continue;
                }
                SNetworkComponent* Net = Registry.try_get<SNetworkComponent>(Entity);
                if (Net == nullptr || Net->OwningConnectionId != Sender.Value) // ownership gate
                {
                    continue;
                }
                State.InterpStates[Guid].Push(ServerNow, Pos, Rot);
            }
        }
    }

    void SNetworkSystem::Update(const FSystemContext& Context) noexcept
    {
        entt::registry& Registry = Context.GetRegistry();

        CWorld* World = Registry.ctx().get<CWorld*>();
        if (World == nullptr)
        {
            return;
        }

        const ENetMode NetMode = World->GetNetMode();
        if (NetMode == ENetMode::Standalone)
        {
            return;
        }

        // Lazily create the per-world net state on the first networked tick; log the role once.
        FNetWorldState* State = Registry.ctx().find<FNetWorldState>();
        if (State == nullptr)
        {
            State = &Registry.ctx().emplace<FNetWorldState>();
            LOG_DISPLAY("[Net] World '{}' net mode = {}", World->GetName().c_str(), NetModeToString(NetMode));
        }

        const bool bServer = IsServerMode(NetMode);

        // Adopt pre-placed entities into stable NetGUIDs once, before any transport activity.
        if (!State->bStableEntitiesAdopted)
        {
            State->bStableEntitiesAdopted = true;
            AdoptStableNetworkEntities(Registry, *State, bServer);
        }

        if (!State->bInitialized)
        {
            State->bInitialized = true;

            // Connection target is data-driven (FWorldContext), set by FEngine::OpenLevel or the editor PIE
            // setup. Defaults to loopback 127.0.0.1 port 7777.
            FWorldContext* WorldCtx = GWorldManager ? GWorldManager->FindContext(World) : nullptr;
            const FString  Host = WorldCtx ? WorldCtx->NetHost : FString("127.0.0.1");
            const uint16   Port = WorldCtx ? WorldCtx->NetPort : NetDefaultPort;

            if (bServer)
            {
                State->Transport.reset(Network::CreateTransport());

                FListenParams Params;
                Params.Port           = Port;
                Params.MaxConnections = 8;
                Params.ChannelCount   = 2;

                if (State->Transport->StartServer(Params))
                {
                    LOG_DISPLAY("[Net] Listen server started on port {}", Port);
                }
                else
                {
                    LOG_ERROR("[Net] Failed to start listen server on port {}", Port);
                }
            }
            else if (GEngine != nullptr && GEngine->HasCarriedConnection())
            {
                // Welcome-driven travel handed us the already-connected transport from the previous world.
                FConnectionHandle Conn;
                uint32 PeerId = ServerPeerId;
                State->Transport        = GEngine->TakeCarriedConnection(Conn, PeerId);
                State->ServerConnection = Conn;
                State->LocalPeerId      = PeerId;
                State->bClientConnected = true;
                LOG_DISPLAY("[Net] Client adopted carried connection after travel (peer id {})", PeerId);
            }
            else // Client, open a fresh connection.
            {
                State->Transport.reset(Network::CreateTransport());

                FConnectParams Params;
                Params.Address.Host = Host;
                Params.Address.Port = Port;
                Params.ChannelCount = 2;

                State->ServerConnection = State->Transport->ConnectToServer(Params);
                LOG_DISPLAY("[Net] Client connecting to {}:{}...", Host.c_str(), Port);
            }
        }

        if (State->Transport == nullptr)
        {
            return;
        }

        // Pump the transport. Surface connect/disconnect, and on the client apply incoming snapshots.
        TVector<FNetworkEvent> Events;
        State->Transport->Service(Events);

        for (const FNetworkEvent& Event : Events)
        {
            switch (Event.Type)
            {
            case ENetworkEventType::Connected:
                if (bServer)
                {
                    ++State->ConnectedClients;
                    State->ConnectedClientIds.push_back(Event.Connection.Value);
                    State->bForceMovementResend = true; // baseline every movement-replicated pose for the new client
                    State->bOwnershipDirty       = true; // and the current ownership table

                    // Tell the new client its unique peer id (= its connection handle here).
                    TVector<uint8> Buffer;
                    FNetArchive Writer(Buffer);
                    uint8  Type   = static_cast<uint8>(ENetMessage::AssignPeerId);
                    uint32 PeerId = Event.Connection.Value;
                    Writer << Type;
                    Writer << PeerId;
                    Net::SendFramed(*State->Transport, Event.Connection, Buffer.data(), static_cast<SIZE_T>(Buffer.size()), 0, ESendMode::Reliable);

                    // Welcome, tell the client which map we're running so it can load the same level.
                    if (FWorldContext* SrvCtx = GWorldManager ? GWorldManager->FindContext(World) : nullptr)
                    {
                        const FString& MapPath = SrvCtx->MapPath;
                        TVector<uint8> WelcomeBuf;
                        FNetArchive WWriter(WelcomeBuf);
                        uint8  WType = static_cast<uint8>(ENetMessage::Welcome);
                        uint16 Len   = static_cast<uint16>(MapPath.size());
                        WWriter << WType;
                        WWriter << Len;
                        if (Len > 0)
                        {
                            WWriter.Serialize(const_cast<char*>(MapPath.data()), Len);
                        }
                        Net::SendFramed(*State->Transport, Event.Connection, WelcomeBuf.data(), static_cast<SIZE_T>(WelcomeBuf.size()), 0, ESendMode::Reliable);
                    }

                    LOG_DISPLAY("[Net][Server] Client {} connected ({} total)", Event.Connection.Value, State->ConnectedClients);
                }
                else
                {
                    State->bClientConnected = true;
                    LOG_DISPLAY("[Net][Client] Connected to server (handle {})", Event.Connection.Value);
                }
                break;

            case ENetworkEventType::Disconnected:
                if (bServer)
                {
                    State->ConnectedClients = (State->ConnectedClients > 0) ? State->ConnectedClients - 1 : 0;
                    auto& Ids = State->ConnectedClientIds;
                    Ids.erase(eastl::remove(Ids.begin(), Ids.end(), Event.Connection.Value), Ids.end());

                    // Release anything this connection owned so it doesn't stay stuck as an orphan proxy.
                    for (entt::entity Entity : Registry.view<SNetworkComponent>())
                    {
                        SNetworkComponent& Net = Registry.get<SNetworkComponent>(Entity);
                        if (Net.OwningConnectionId == Event.Connection.Value)
                        {
                            Net.OwningConnectionId = 0;
                            State->bOwnershipDirty  = true;
                        }
                    }
                    LOG_DISPLAY("[Net][Server] Client {} disconnected ({} total)", Event.Connection.Value, State->ConnectedClients);
                }
                else
                {
                    State->bClientConnected = false;
                    State->LocalPeerId      = ServerPeerId;
                    LOG_DISPLAY("[Net][Client] Disconnected from server");
                }
                break;

            case ENetworkEventType::Data:
            {
                // Every packet is a batch of length-framed messages; split + dispatch each by its type byte.
                Net::ForEachFramedMessage(Event.Data.data(), static_cast<SIZE_T>(Event.Data.size()),
                    [&](const uint8* Msg, SIZE_T MsgSize)
                {
                    if (MsgSize == 0)
                    {
                        return;
                    }
                    switch (static_cast<ENetMessage>(Msg[0]))
                    {
                    case ENetMessage::TransformSnapshot:
                        if (!bServer)
                        {
                            ApplyTransformSnapshot(Registry, *State, Msg, MsgSize);
                        }
                        break;
                    case ENetMessage::AssignPeerId:
                        if (!bServer)
                        {
                            FNetArchive Reader(Msg, MsgSize);
                            uint8  Type   = 0;
                            uint32 PeerId = 0;
                            Reader << Type;
                            Reader << PeerId;
                            if (!Reader.HasError())
                            {
                                State->LocalPeerId = PeerId;
                                LOG_DISPLAY("[Net][Client] Assigned peer id {}", PeerId);
                            }
                        }
                        break;
                    case ENetMessage::OwnershipUpdate:
                        if (!bServer)
                        {
                            ApplyOwnershipUpdate(Registry, *State, Msg, MsgSize);
                        }
                        break;
                    case ENetMessage::SpawnEntity:
                        if (!bServer)
                        {
                            ApplySpawnEntity(World, Registry, *State, Event.Connection.Value, Msg, MsgSize);
                        }
                        break;
                    case ENetMessage::DespawnEntity:
                        if (!bServer)
                        {
                            ApplyDespawnEntity(World, *State, Msg, MsgSize);
                        }
                        break;
                    case ENetMessage::PropertyUpdate:
                        if (!bServer)
                        {
                            ApplyPropertyUpdate(World, Registry, *State, Event.Connection.Value, Msg, MsgSize);
                        }
                        break;
                    case ENetMessage::ClientTransform:
                        if (bServer)
                        {
                            ApplyClientTransform(Registry, *State, Event.Connection, Context.GetTime(), Msg, MsgSize);
                        }
                        break;
                    case ENetMessage::Welcome:
                        if (!bServer)
                        {
                            FNetArchive Reader(Msg, MsgSize);
                            uint8  Type = 0;
                            uint16 Len  = 0;
                            Reader << Type;
                            Reader << Len;
                            FString MapPath;
                            if (!Reader.HasError() && Len > 0 && Len < 4096)
                            {
                                MapPath.resize(Len);
                                Reader.Serialize(MapPath.data(), Len);
                            }
                            // Load the server's level if we aren't already on it. The live connection is
                            // carried across the travel. PIE clients share the server's map, so no travel.
                            if (!MapPath.empty())
                            {
                                FWorldContext* CliCtx = GWorldManager ? GWorldManager->FindContext(World) : nullptr;
                                const FString Current = CliCtx ? CliCtx->MapPath : FString();
                                if (Current != MapPath && GEngine != nullptr)
                                {
                                    LOG_DISPLAY("[Net][Client] Welcome -> loading server map '{}'", MapPath.c_str());
                                    GEngine->Travel(FStringView(MapPath.c_str(), MapPath.size()));
                                }
                            }
                        }
                        break;
                    case ENetMessage::ClientReady:
                        if (bServer)
                        {
                            // Late-join initial sync, catch this connection up to the current world without
                            // re-broadcasting spawns to everyone. Transforms re-baseline via the keyframe.
                            State->bForceMovementResend = true;
                            State->bOwnershipDirty       = true;

                            // Build the joiner's spawn batch first (assigns its object/asset indices), then
                            // send the index tables, then the spawns, so indices resolve before the spawns.
                            TVector<uint8> SpawnBatch;
                            BuildExistingSpawnBatch(Registry, *State, SpawnBatch);

                            if (!State->OutObjects.IndexToGuid.empty())
                            {
                                TVector<uint32> AllIndices;
                                AllIndices.reserve(State->OutObjects.IndexToGuid.size());
                                for (const auto& Pair : State->OutObjects.IndexToGuid)
                                {
                                    AllIndices.push_back(Pair.first);
                                }
                                TVector<uint8> ExportMsg;
                                Net::BuildObjectExport(State->OutObjects, AllIndices, ExportMsg);
                                Net::SendFramed(*State->Transport, Event.Connection, ExportMsg.data(), static_cast<SIZE_T>(ExportMsg.size()), 0, ESendMode::Reliable);
                            }

                            if (!State->OutAssets.IndexToRef.empty()) // asset index table for the joiner
                            {
                                TVector<uint32> AllIndices;
                                AllIndices.reserve(State->OutAssets.IndexToRef.size());
                                for (const auto& Pair : State->OutAssets.IndexToRef)
                                {
                                    AllIndices.push_back(Pair.first);
                                }
                                TVector<uint8> ExportMsg;
                                Net::BuildAssetExport(State->OutAssets, AllIndices, ExportMsg);
                                Net::SendFramed(*State->Transport, Event.Connection, ExportMsg.data(), static_cast<SIZE_T>(ExportMsg.size()), 0, ESendMode::Reliable);
                            }

                            if (!SpawnBatch.empty())
                            {
                                State->Transport->Send(Event.Connection, SpawnBatch.data(), static_cast<SIZE_T>(SpawnBatch.size()), 0, ESendMode::Reliable);
                            }

                            // Replay stable entities the server destroyed at runtime. The joiner loaded the
                            // level fresh, so tell it to remove the copies it would have created.
                            if (!State->DestroyedStableGuids.empty())
                            {
                                TVector<uint8> DespawnBatch;
                                for (uint32 Guid : State->DestroyedStableGuids)
                                {
                                    TVector<uint8> Buffer;
                                    FNetArchive Writer(Buffer);
                                    uint8  Type = static_cast<uint8>(ENetMessage::DespawnEntity);
                                    uint32 G    = Guid;
                                    Writer << Type;
                                    Writer << G;
                                    Net::AppendFramedMessage(DespawnBatch, Buffer.data(), static_cast<SIZE_T>(Buffer.size()));
                                }
                                State->Transport->Send(Event.Connection, DespawnBatch.data(), static_cast<SIZE_T>(DespawnBatch.size()), 0, ESendMode::Reliable);
                            }

                            LOG_DISPLAY("[Net][Server] Client {} ready; sent initial state ({} spawn bytes)", Event.Connection.Value, static_cast<int>(SpawnBatch.size()));
                        }
                        break;
                    case ENetMessage::ObjectExport:
                        // Store the sender's exports in its own incoming map since the index space is
                        // sender-owned. The server needs this for client-to-server RPC object args.
                        Net::ApplyObjectExport(State->InObjects[Event.Connection.Value], Msg, MsgSize);
                        break;
                    case ENetMessage::AssetExport:
                        Net::ApplyAssetExport(State->InAssets[Event.Connection.Value], Msg, MsgSize);
                        break;
                    case ENetMessage::ScriptRpc:
                        // Either side can receive (server gets Server RPCs; client gets Multicast/Client).
                        // The ownership/authority gate lives in ReceiveScriptRpc.
                        Net::ReceiveScriptRpc(World, Event.Connection, Msg, MsgSize);
                        break;
                    }
                });
                break;
            }
            }
        }

        // Roles fall out of net mode + ownership; recompute every tick (cheap, few networked entities).
        RefreshNetRoles(Registry, *State, bServer);

        if (bServer)
        {
            // Smooth client-owned entities from their received-pose buffer before relaying. The host's own
            // entities aren't buffered so they stay live. Done first so the relay snapshot uses the result.
            InterpolateProxies(Registry, *State, Context.GetTime());

            if (State->ConnectedClients > 0)
            {
                // Accumulate this tick's messages into two batches (one per reliability tier) and flush each
                // as a single datagram, one ENet header and ack instead of one per message.
                TVector<uint8> ReliableBatch;
                TVector<uint8> UnreliableBatch;

                // Reliable, generic entity replication. Spawn/despawn dynamic entities, then property deltas.
                ReplicateSpawns(Registry, *State, ReliableBatch);
                ReplicateStableDespawns(Registry, *State, ReliableBatch);
                ReplicateDirtyProperties(Registry, *State, ReliableBatch);

                // Ownership is reliable state; (re)broadcast the table on change or a new join.
                if (State->bOwnershipDirty)
                {
                    BroadcastOwnership(Registry, ReliableBatch);
                    State->bOwnershipDirty = false;
                }

                // Periodic keyframe, re-send every replicated pose so a dropped delta self-heals within the
                // interval. Deltas still cover per-frame movement.
                const float KeyframeInterval = CVarTransformKeyframeInterval.GetValue();
                if (KeyframeInterval > 0.0f)
                {
                    State->TimeSinceKeyframe += static_cast<float>(Context.GetDeltaTime());
                    if (State->TimeSinceKeyframe >= KeyframeInterval)
                    {
                        State->bForceMovementResend = true;
                        State->TimeSinceKeyframe    = 0.0f;
                    }
                }

                BroadcastTransformSnapshot(Registry, UnreliableBatch, State->bForceMovementResend,
                    static_cast<float>(Context.GetDeltaTime()), static_cast<float>(Context.GetTime()));
                State->bForceMovementResend = false;

                // Export newly-assigned net indices before the reliable batch that references them, on the
                // same ordered channel, framed so clients resolve them in the batch.
                if (!State->OutObjects.PendingExports.empty())
                {
                    TVector<uint8> ExportMsg;
                    Net::BuildObjectExport(State->OutObjects, State->OutObjects.PendingExports, ExportMsg);
                    Net::BroadcastFramed(*State->Transport, ExportMsg.data(), static_cast<SIZE_T>(ExportMsg.size()), 0, ESendMode::Reliable);
                    State->OutObjects.PendingExports.clear();
                }
                if (!State->OutAssets.PendingExports.empty())
                {
                    TVector<uint8> ExportMsg;
                    Net::BuildAssetExport(State->OutAssets, State->OutAssets.PendingExports, ExportMsg);
                    Net::BroadcastFramed(*State->Transport, ExportMsg.data(), static_cast<SIZE_T>(ExportMsg.size()), 0, ESendMode::Reliable);
                    State->OutAssets.PendingExports.clear();
                }

                if (!ReliableBatch.empty())
                {
                    State->Transport->Broadcast(ReliableBatch.data(), static_cast<SIZE_T>(ReliableBatch.size()), 0, ESendMode::Reliable);
                }
                if (!UnreliableBatch.empty())
                {
                    State->Transport->Broadcast(UnreliableBatch.data(), static_cast<SIZE_T>(UnreliableBatch.size()), 0, ESendMode::UnreliableSequenced);
                }
            }
        }
        else
        {
            // Keep proxy physics from fighting replication (idempotent; acts once per body).
            ConfigureProxyPhysics(Registry, World);

            // Once connected (fresh or carried-across-travel), tell the server we've loaded and want the
            // full world baseline. One-shot; the server re-emits spawns/ownership/poses on receipt.
            if (State->bClientConnected && !State->bClientReadySent)
            {
                State->bClientReadySent = true;
                TVector<uint8> Buffer;
                FNetArchive Writer(Buffer);
                uint8 Type = static_cast<uint8>(ENetMessage::ClientReady);
                Writer << Type;
                Net::SendFramed(*State->Transport, State->ServerConnection, Buffer.data(), static_cast<SIZE_T>(Buffer.size()), 0, ESendMode::Reliable);
            }

            // Client-authoritative movement, push the pose of entities we control up to the server.
            if (State->bClientConnected)
            {
                SendOwnedTransforms(Registry, *State, static_cast<float>(Context.GetDeltaTime()));
            }

            // Smooth every SimulatedProxy from its sample buffer (runs every frame, not just on packet
            // receipt, so motion stays smooth between the throttled updates).
            InterpolateProxies(Registry, *State, Context.GetTime());
        }
    }
}
