# Runtime Entity Components ("EntityComponent")

A runtime-authored CStruct usable as an ECS component — no C++ class required. A designer
defines a component *type* (the runtime CStruct: field list + defaults) as an asset, attaches
instances to entities, and the engine stores them contiguously, serializes them with the world,
and migrates ("fixes up") live and on-disk instances when the type's schema changes.

Built on the existing `FPropertyBag` (the runtime-minted CStruct + value buffer) and the
schema/instance split already proven by `CDataAssetSchema` / `CDataAsset`.

## Why a custom EnTT storage (Design B)

Components today require a compile-time C++ type, reflected via `Meta::RegisterComponentMeta<T>()`.
EnTT's `basic_storage<T>` is fixed-size (`sizeof(T)` baked at compile time). To store a
variable-schema bag contiguously with **no pointer indirection**, we inject a custom
runtime-stride storage:

- `basic_sparse_set<entt::entity>` is fully type-erased; element access is delegated through
  the virtuals `get_at`, `try_emplace`, `swap_or_move`, `pop`, `pop_all`, `reserve`.
- `entt::storage_type<Carrier>` is a documented customization point: specialize it and
  `registry.storage<Carrier>(id)` mints our class.
- `registry.destroy(e)` routes through `pool->remove` → base `erase` → virtual `pop`, so our
  per-field destructors run on entity destroy **for free**.

A fixed-size carrier (Design A) was rejected: it caps element size and still needs per-field
ctor/dtor for `FString`/`TObjectPtr` fields (entt would memcpy them, corrupting/leaking).

## Two representations, two fixup paths

1. **Persisted** = schema-id + tagged bytes (`SerializeTaggedProperties`). Self-describing,
   schema-evolution-safe.
2. **Live** = contiguous Stride buffer (by-offset, no tags), built from a layout + revision.

- **Case A — type changed while world unloaded:** resolved automatically on load. Tagged-property
  read reconciles the stream against the *current* layout (matched fields assigned w/ numeric
  coercion, removed fields skipped by size, new fields keep schema defaults). No explicit step.
- **Case B — type changed while world loaded:** the schema bumps a `Revision`; each storage
  records its `BoundRevision`. A revision mismatch triggers an in-place storage migration
  (realloc new Stride × count; per-element name-matched value migration; per-field destruct of
  removed/old fields). Lazy on access **and** a per-frame sweep at `FrameStart`.

## Work breakdown

### Runtime
1. **`FPropertyBag` element-lifecycle helpers** (`PropertyBag.h/.cpp`): static
   `ConstructValueInto` / `DestructValueIn` / `CopyValueInto` operating on an external buffer
   given `(Layout, Fields)`; plus `GetBufferSize()` / `GetBufferAlignment()`. Reuses the existing
   `ResolveField` / `CopyBagValue` internals (no duplication of the type switch).
2. **`CEntityComponentType`** asset (`Assets/AssetTypes/EntityComponent/`): a CObject holding an
   `FPropertyBag SchemaBag` (fields + defaults) + `uint32 SchemaRevision`. `IsAsset()=true`,
   `Serialize` (CObject + bag + revision), `BumpRevision`, `OnDestroy` (orphan live storages),
   `GetStorageId()` = stable hash of the asset GUID.
3. **`FRuntimeComponentStorage`** (`World/Entity/RuntimeComponent.h/.cpp`):
   - `struct FDynamicComponentTag {}` carrier + `entt::storage_type<FDynamicComponentTag>`
     specialization → `FRuntimeComponentStorage`.
   - `: entt::basic_sparse_set<entt::entity>`; ctor passes `type_id<FDynamicComponentTag>()` so
     the registry's `assure` type-assert passes.
   - Contiguous `uint8*` packed buffer at runtime `Stride`. Overrides `get_at`, `try_emplace`
     (copy from value or seed from defaults), `pop`/`pop_all` (per-field destruct), `swap_or_move`,
     `reserve`.
   - `BindLayout(CEntityComponentType*)` (post-construct), `MigrateTo(...)` (Case B), holds
     `CEntityComponentType* SchemaType` + `FGuid SchemaGuid` + `uint32 BoundRevision`.
4. **World/ECS API** (`ECS::Utils`, `EntityUtils.h/.cpp`): `GetOrCreateRuntimeStorage`,
   `FindRuntimeStorage`, `AddRuntimeComponent`, `RemoveRuntimeComponent`, `GetRuntimeComponent`,
   `HasRuntimeComponent`, `ForEachRuntimeComponent`, `RefreshRuntimeComponentSchemas` (sweep).
   Storage identified in generic loops via `set.info() == type_id<FDynamicComponentTag>()`.
5. **Serialization** (`SerializeEntity` in `EntityUtils.cpp`): in the write storage loop, detect
   dynamic storages and emit a `@RuntimeComponent` sentinel entry: `Ar << SchemaObj` (canonical
   object-ref import) then `Layout->SerializeTaggedProperties`. Read branch resolves the schema,
   gets/creates the storage, default-emplaces, then tagged-reads (= Case A fixup).
6. **Duplicate/Copy:** `DuplicateEntity`'s generic `Storage.push(To, value(Source))` already
   routes through our copying `try_emplace`. Run `RemapEntityReferences` over the layout too
   (forward-safe; no-op without Entity-tagged fields).
7. **Fixup sweep:** `CWorld::Update` `FrameStart` calls `RefreshRuntimeComponentSchemas`.

### Editor
8. **Factory** `CEntityComponentTypeFactory` (category "Gameplay"), creatable.
9. **Schema editor** `FEntityComponentTypeEditorTool` — mirrors `FDataAssetSchemaEditorTool`
   (Add Field row + SearchableCombo type picker + PropertyTable over defaults + per-row delete);
   bumps revision on structural edits. Registered in `EditorUI::OpenAssetEditor`.
10. **Entity inspector** — a "Runtime Components" section: list via `ForEachRuntimeComponent`
    (PropertyTable over `Storage.Layout` + `Storage.value(Entity)`), an "Add Runtime Component"
    picker over loaded `CEntityComponentType`s, and per-component remove.

### Build
- New `.cpp/.h` require `Tools/premake5.exe vs2022` before MSBuild (modules glob sources).

## Deferred (explicitly out of scope for v1)
- ECS-side Lua bindings for runtime components.
- Entity-reference field type inside bags (kept forward-safe but unauthored).
- True field *rename* tracking (rename = drop+add-defaulted, matching DataAsset behavior).
- entt views/groups over runtime components (iterate the named storage directly instead).
