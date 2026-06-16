#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Platform/GenericPlatform.h"
#include "Core/UpdateStage.h"

namespace Lumina
{
    struct FScriptPropertyOverrides;
    struct FSystemContext;
    enum class EUpdateStage : uint8;
    namespace Scripting { struct FScriptExportSchema; struct FScriptPropertyEntry; }
}

// FDotNetHost embeds CoreCLR (via the bundled runtime under External/DotNet) and
// loads the LuminaSharp managed bootstrap. Phase 1: boot + ABI handshake only.
namespace Lumina::DotNet
{
    // Bumped whenever the native<->managed boundary ABI changes. The managed
    // Host carries the same constant; the bootstrap handshake rejects a mismatch
    // rather than letting a stale assembly call through a changed table.
    // v2: added the EntitySystem bridge (Enumerate/Create/Tick/DestroyEntitySystem).
    inline constexpr int32 GAbiVersion = 2;

    // Boots the embedded runtime and runs the managed handshake. Non-fatal: on any
    // failure it logs and leaves C# scripting disabled (IsInitialized() == false).
    // Game thread only.
    RUNTIME_API void Initialize();

    // Drops boundary state. CoreCLR itself is not unloaded mid-process; collectible
    // per-script ALCs (Phase 5) are what get torn down. Game thread only.
    RUNTIME_API void Shutdown();

    // Per-frame pump: dispatches managed script lifecycle (OnUpdate). Game thread only.
    RUNTIME_API void Tick();

    // Gathers every Scripts/*.cs across all mounted VFS roots (game + plugins + engine),
    // recompiles them in-process, and hot-swaps the live script generation, no engine
    // restart. Used for the initial load and the manual "Reload Scripts" action
    // (console command "dotnet.reload" / editor button). Game thread only.
    RUNTIME_API void ReloadScripts();

    // Writes/refreshes an SDK-style .csproj into every script root (game + each plugin's Scripts/)
    // referencing the engine's LuminaSharp.dll, so an IDE gives full completion when editing scripts.
    // IDE-only: scripts still compile at runtime via Roslyn. Console command "dotnet.genprojects".
    RUNTIME_API void GenerateScriptProjects();

    RUNTIME_API bool IsInitialized();

    //~ EntityScript archetype bridge, driven by SCSharpScriptSystem. Game thread only.

    // Current script generation; bumps on every successful (re)load. The ECS system rebinds
    // an entity's managed instance when this changes (initial attach + hot reload, one path).
    RUNTIME_API int32 GetScriptGeneration();

    // Instantiates the EntityScript of the given full type name for an entity in a world; returns an
    // opaque managed instance handle (a strong GCHandle the caller stores on the component), or nullptr
    // on failure. The handle is the link for every later per-instance call - there is no lookup table.
    RUNTIME_API void* CreateEntityScript(FStringView TypeName, uint64 World, uint32 Entity);

    // Runs OnReady on a freshly-attached instance (after the whole world's batch has attached).
    RUNTIME_API void OnReadyScript(void* Instance);

    // Ticks a batch of script instances, ONE call per world per frame. Instances points at Count handles
    // the caller collected from its component view; managed dispatches OnUpdate directly in a tight loop.
    RUNTIME_API void UpdateScripts(void* const* Instances, int32 Count, float DeltaSeconds);

    // Runs OnDetach and releases the managed instance handle.
    RUNTIME_API void DestroyEntityScript(void* Instance);

    // Full type names of every loaded EntityScript, for the editor's script-picker dropdown. Empty
    // when scripting is disabled / not yet loaded. Game thread only.
    RUNTIME_API void GatherEntityScriptTypes(TVector<FString>& OutTypeNames);

    //~ EntitySystem bridge: a C# system the native stage scheduler ticks as a first-class system (one
    //  instance per world, scheduled into an FStageSlot whose Self is the managed GCHandle). Game thread only.

    // One discovered managed system type: its full name + declared stage/priority ([EntitySystem]).
    struct FManagedSystemDesc
    {
        FString      TypeName;
        EUpdateStage Stage = EUpdateStage::PrePhysics;
        int32        Priority = 128;
    };

    // Gathers every loaded EntitySystem (full name + stage + priority). Empty when scripting is disabled.
    RUNTIME_API void GatherManagedSystemDescs(TVector<FManagedSystemDesc>& Out);

    // Instantiates the named EntitySystem for a world; returns a strong GCHandle (as void*) the caller
    // stores as the FStageSlot Self, or nullptr on failure.
    RUNTIME_API void* CreateManagedSystem(FStringView TypeName, uint64 World);

    // Runs OnTeardown and releases the managed instance handle.
    RUNTIME_API void DestroyManagedSystem(void* Handle);

    // Ticks one managed system instance (forwards to OnUpdate with the system context). One call per
    // managed system per stage, from the shared native shim in CWorld.
    RUNTIME_API void TickManagedSystem(void* Handle, const FSystemContext* Context);

    // Invokes a collision/overlap callback on the script Instance (Kind: 0=ContactBegin, 1=ContactEnd,
    // 2=OverlapBegin, 3=OverlapEnd). Event points at an SCollisionEvent (passed opaque to keep this
    // header lean). Game thread only, call after the physics step.
    RUNTIME_API void DispatchScriptCollision(void* Instance, int32 Kind, const void* Event);

    // Bitmask of collision callbacks the script overrides (bit (1<<Kind)); 0 if none / not bound. Lets
    // the caller skip the managed crossing for callbacks the script doesn't handle.
    RUNTIME_API int32 GetScriptCallbackFlags(void* Instance);

    // Delivers one discrete input event to a script's OnInput. Flat args (no struct marshaling): Type =
    // EInputEventType, KeyCode = EKey/EMouseKey, bMouse/Mods/bRepeat flags, mouse pos/delta/scroll. The
    // OnInput-overridden bit in GetScriptCallbackFlags is (1<<4). Game thread, during the world update.
    RUNTIME_API void DispatchScriptInput(void* Instance, int32 Type, int32 KeyCode, int32 bMouse, int32 Mods,
        int32 bRepeat, double MouseX, double MouseY, double DeltaX, double DeltaY, double Scroll);

    //~ Exported [Property] schema bridge (editor inspector + serialization). Game thread only.

    // Builds the [Property] schema + default values for a C# script type (via managed reflection).
    // Scalar kinds (bool/int/double/string/vec2-4). Returns false if the type isn't loaded.
    RUNTIME_API bool GatherScriptSchema(FStringView ScriptClass, Scripting::FScriptExportSchema& OutSchema, TVector<Scripting::FScriptPropertyEntry>& OutDefaults);

    // Applies per-instance override values onto a live script Instance via a recursive value blob
    // (supports nested structs + arrays; schema-drift fields are skipped). Call after CreateEntityScript
    // + ReconcileOverrides.
    RUNTIME_API void ApplyScriptProperties(void* Instance, const FScriptPropertyOverrides& Overrides);
}
