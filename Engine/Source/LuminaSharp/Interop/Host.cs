using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace LuminaSharp;

/// <summary>
/// The managed entry surface FDotNetHost calls. Every entry is [UnmanagedCallersOnly] (blittable args
/// only) and guards the boundary: nothing may unwind into native code, so each catch funnels through
/// <see cref="Interop.LogException"/>. The entry method names here are a name-based ABI contract with
/// the native host (FDotNetHost looks them up by string) and must not be renamed.
///
/// The per-instance entries take an IntPtr that IS a strong GCHandle to the managed EntityScript,
/// stored on the native SCSharpScriptComponent, no managed lookup table. The world's native ECS
/// system drives create -> ready -> update -> destroy and batches OnUpdate into one UpdateScripts call.
/// </summary>
public static unsafe partial class Host
{
    // Must equal Lumina::DotNet::GAbiVersion.
    // v4: LoadScripts takes per-unit assembly buckets (FSourceAssembly) instead of a flat source list.
    // v5: native->managed exports resolved by name (ManagedExportRegistry) instead of a mirrored struct/hash.
    // v6: EnumerateEntitySystems sink carries declared read/write component-ops tokens (parallel C# systems).
    private const int AbiVersion = 6;

    // The logical module name for the engine module that hosts this assembly (Runtime). NativeBindings
    // resolves it to the loaded native handle via ModuleHandle; per-module bindings use their own names.
    public const string NativeLibrary = "LuminaNative";

    private static ScriptManager? Scripts;
    private static IntPtr NativeModule;
    private static readonly Dictionary<string, IntPtr> ModuleHandles = new();

    // Bootstrap-critical exports, resolved directly from the host (Runtime) module. Every other binding
    // resolves lazily through ModuleHandle (which uses ResolveModuleHandlePtr). No [DllImport] anywhere.
    private static delegate* unmanaged[Cdecl]<int, int, int> NativeSelfTestPtr;
    private static delegate* unmanaged[Cdecl]<byte*, int, IntPtr> ResolveModuleHandlePtr;

    // Bootstrap is the ONE entry the native host resolves by name (hostfxr); it is not in the export table.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int Bootstrap(FBootstrapArgs* Args)
    {
        try
        {
            if (Args == null || Args->Exports == null)
            {
                return 1;
            }
            if (Args->AbiVersion != AbiVersion)
            {
                return 2;
            }

            // NativeModule must be set before any binding resolves (Native's static init may run here).
            NativeModule = Args->NativeModule;
            Native.SetExports(*Args->Exports);

            // Register every engine [ManagedExport] by name; native resolves each via ResolveManagedExport.
            ManagedExportTable.RegisterEngineExports();

            NativeSelfTestPtr = (delegate* unmanaged[Cdecl]<int, int, int>)NativeBindings.ResolveFrom(NativeModule, "LuminaSharp_NativeSelfTest");
            ResolveModuleHandlePtr = (delegate* unmanaged[Cdecl]<byte*, int, IntPtr>)NativeBindings.ResolveFrom(NativeModule, "LuminaSharp_ResolveModuleHandle");

            int Sum = NativeSelfTestPtr != null ? NativeSelfTestPtr(2, 3) : -1;
            Native.Log(Sum == 5 ? ELogLevel.Info : ELogLevel.Error,
                Sum == 5 ? "C#->native function-pointer path OK." : $"C# interop self-test FAILED (got {Sum}).");

            // Cross-check every blittable C#/C++ mirror's size before any of them crosses the boundary. A
            // mismatch here means a struct layout drifted; running anyway would silently corrupt memory.
            if (!LayoutValidator.ValidateAll())
            {
                return 4;
            }

            Scripts = new ScriptManager();
            Native.Log(ELogLevel.Info, $"LuminaSharp online (runtime {RuntimeInformation.FrameworkDescription}).");
            return 0;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return 3;
        }
    }

    /// <summary>Resolves a native->managed export to its function pointer by name (engine or script/plugin),
    /// or IntPtr.Zero if unknown. Native resolves this entry itself via hostfxr (like Bootstrap), then uses it
    /// to look up every other managed entry, so there is no hand-mirrored export struct.</summary>
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static IntPtr ResolveManagedExport(byte* Name, int Length)
    {
        try
        {
            return ManagedExportRegistry.Resolve(Interop.GetString(Name, Length));
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return IntPtr.Zero;
        }
    }

    /// <summary>Resolves (and caches) a native module's loaded handle by name; "LuminaNative" is the host
    /// (Runtime) module. The generated bindings call this through <see cref="NativeBindings.Resolve"/>.</summary>
    public static IntPtr ModuleHandle(string Name)
    {
        if (Name == NativeLibrary)
        {
            return NativeModule;
        }

        lock (ModuleHandles)
        {
            if (ModuleHandles.TryGetValue(Name, out IntPtr Handle))
            {
                return Handle;
            }

            Handle = ResolveModule(Name);
            // Only cache a SUCCESSFUL resolve. A miss can be transient -- e.g. a binding whose static
            // initializer runs during early bootstrap, before ResolveModuleHandlePtr is wired -- so retry
            // on the next call rather than poisoning every future lookup of this module with a zero handle.
            if (Handle != IntPtr.Zero)
            {
                ModuleHandles[Name] = Handle;
            }
            return Handle;
        }
    }

    private static IntPtr ResolveModule(string Name)
    {
        if (ResolveModuleHandlePtr == null)
        {
            return IntPtr.Zero;
        }

        Span<byte> Scratch = stackalloc byte[256];
        Interop.FInteropString Encoded = new(Name, Scratch);
        try
        {
            return ResolveModuleHandlePtr(Encoded.Pointer, Encoded.Length);
        }
        finally
        {
            Encoded.Free();
        }
    }

    /// <summary>Resolves a type from the loaded script generation by full name (used by the dynamic invoker
    /// in <see cref="ManagedCalls"/>; engine types resolve from LuminaSharp.dll directly). Null if not found
    /// / no scripts loaded.</summary>
    internal static Type? ResolveScriptType(string Name)
    {
        return Scripts?.EntityScripts?.FindType(Name);
    }

    /// <summary>Current script generation; the native side rebinds entity scripts when it changes (hot reload).</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int GetGeneration()
    {
        return Scripts?.Generation ?? 0;
    }

    /// <summary>Fills the editor's C# Diagnostics snapshot (managed heap, GC counters, and collectible-ALC /
    /// generation health). Returns 1 on success. Editor-only caller; never invoked on the runtime path, so it
    /// costs nothing in a packaged game. GC + ALC reads are always valid even with no scripts loaded. When
    /// ForceCollect != 0 it runs a full blocking GC first (the tool's "Force GC" button only, never on the
    /// periodic poll), which both reclaims and lets a pending collectible ALC actually unload.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int GetRuntimeDiagnostics(IntPtr OutPtr, int ForceCollect)
    {
        try
        {
            if (OutPtr == IntPtr.Zero)
            {
                return 0;
            }

            if (ForceCollect != 0)
            {
                for (int Pass = 0; Pass < 3; Pass++)
                {
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                }
            }

            ref FScriptDiagnostics Diag = ref *(FScriptDiagnostics*)OutPtr;
            Diag = default;

            GCMemoryInfo Info = GC.GetGCMemoryInfo();
            Diag.ManagedHeapBytes    = GC.GetTotalMemory(false);
            Diag.HeapSizeBytes       = Info.HeapSizeBytes;
            Diag.FragmentedBytes     = Info.FragmentedBytes;
            Diag.CommittedBytes      = Info.TotalCommittedBytes;
            Diag.TotalAllocatedBytes = GC.GetTotalAllocatedBytes(false);
            Diag.WorkingSetBytes     = Environment.WorkingSet;
            Diag.PauseTimePercentage = Info.PauseTimePercentage;
            ReadOnlySpan<TimeSpan> Pauses = Info.PauseDurations;
            Diag.LastPauseMs         = Pauses.Length > 0 ? Pauses[Pauses.Length - 1].TotalMilliseconds : 0.0;
            Diag.Gen0Collections     = GC.CollectionCount(0);
            Diag.Gen1Collections     = GC.CollectionCount(1);
            Diag.Gen2Collections     = GC.CollectionCount(2);
            Diag.PinnedObjects       = (int)Info.PinnedObjectsCount;

            Diag.Generation        = Scripts?.Generation ?? 0;
            Diag.EntityScriptCount = Scripts?.EntityScripts?.TypeNames.Count ?? 0;
            Diag.EntitySystemCount = Scripts?.EntitySystems?.TypeCount ?? 0;
            Diag.LoadedTypeCount   = Scripts?.LoadedTypeCount ?? 0;
            Diag.ScriptsOnline     = Scripts?.EntityScripts != null ? 1 : 0;

            // How many collectible script generations CoreCLR still has loaded. 1 == healthy (only the
            // current generation). A count that climbs across reloads is a real ALC unload leak; a brief
            // residual right after a reload is the normal asynchronous unload settling.
            int Alive = 0;
            int Oldest = int.MaxValue;
            const string Prefix = "GameScripts.Gen";
            foreach (AssemblyLoadContext Context in AssemblyLoadContext.All)
            {
                if (Context.Name is string Name && Name.StartsWith(Prefix, StringComparison.Ordinal)
                    && int.TryParse(Name.AsSpan(Prefix.Length), out int Gen))
                {
                    Alive++;
                    if (Gen < Oldest)
                    {
                        Oldest = Gen;
                    }
                }
            }
            Diag.AliveScriptAlcCount   = Alive;
            Diag.OldestAliveGeneration = Alive > 0 ? Oldest : 0;

            return 1;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return 0;
        }
    }

    /// <summary>Instantiates an EntityScript of the given full type name for an entity; returns a strong
    /// GCHandle (as IntPtr) the native component stores, or IntPtr.Zero on failure.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static IntPtr CreateEntityScript(byte* TypeName, int TypeNameLength, ulong World, uint Entity)
    {
        try
        {
            return Scripts?.EntityScripts?.Create(Interop.GetString(TypeName, TypeNameLength), World, Entity) ?? IntPtr.Zero;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return IntPtr.Zero;
        }
    }

    /// <summary>Runs OnReady on a freshly-attached script (after all of the world's siblings attached).</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void OnReadyScript(IntPtr Handle)
    {
        try
        {
            Scripts?.EntityScripts?.OnReady(Handle);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Ticks a batch of scripts (one call per world per frame). Handles points at Count strong
    /// GCHandles; the managed loop dispatches OnUpdate directly.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void UpdateScripts(IntPtr* Handles, int Count, float DeltaTime)
    {
        try
        {
            Scripts?.EntityScripts?.Update(Handles, Count, DeltaTime);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Dispatches OnFixedUpdate to a batch of scripts (one call per fixed step per world). Driven by
    /// the native fixed-update accumulator at the physics fixed rate, before the physics step.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void FixedUpdateScripts(IntPtr* Handles, int Count, float FixedDeltaTime)
    {
        try
        {
            Scripts?.EntityScripts?.FixedUpdate(Handles, Count, FixedDeltaTime);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void DestroyEntityScript(IntPtr Handle)
    {
        try
        {
            Scripts?.EntityScripts?.Destroy(Handle);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Routes a collision/overlap callback to a script instance (kind: 0=ContactBegin,
    /// 1=ContactEnd, 2=OverlapBegin, 3=OverlapEnd). Called on the game thread after the physics step.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void DispatchCollision(IntPtr Handle, int Kind, Lumina.SCollisionEvent* Event)
    {
        try
        {
            Scripts?.EntityScripts?.Dispatch(Handle, Kind, in *Event);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Routes one discrete input event to a script's OnInput. Flat args (no struct marshaling):
    /// Type = EInputEventType, KeyCode = EKey/EMouseKey, IsMouse/Mods/Repeat flags, mouse pos/delta/scroll.
    /// Called on the game thread during the world update for each event this frame.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void DispatchInput(IntPtr Handle, int Type, int KeyCode, int IsMouse, int Mods, int Repeat,
        double MouseX, double MouseY, double DeltaX, double DeltaY, double Scroll)
    {
        try
        {
            Lumina.InputEvent Event = new((Lumina.EInputEventType)Type, KeyCode, IsMouse != 0, Mods, Repeat != 0,
                MouseX, MouseY, DeltaX, DeltaY, Scroll);
            Scripts?.EntityScripts?.DispatchInput(Handle, in Event);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Routes an AI perception callback to a script instance (kind: 7=OnTargetPerceived,
    /// 8=OnTargetLost). Called on the game thread during the world update by SPerceptionSystem.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void DispatchPerception(IntPtr Handle, int Kind, Lumina.SPerceptionEvent* Event)
    {
        try
        {
            Scripts?.EntityScripts?.DispatchPerception(Handle, Kind, in *Event);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Bitmask of collision callbacks the script overrides, so native skips the crossing for
    /// the rest (bit (1&lt;&lt;kind), matching DispatchCollision's kinds).</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int GetScriptCallbackFlags(IntPtr Handle)
    {
        try
        {
            return Scripts?.EntityScripts?.CallbackFlags(Handle) ?? 0;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return 0;
        }
    }

    /// <summary>Writes a script type's [Property] schema + defaults to a recursive blob (supports nested
    /// structs + arrays) and hands it to a native sink. sink(ctx, bytes, len) is called once.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void GetScriptSchema(byte* ScriptClass, int ClassLength, IntPtr Sink, IntPtr Context)
    {
        try
        {
            byte[]? Blob = Scripts?.EntityScripts?.Schema(Interop.GetString(ScriptClass, ClassLength));
            if (Blob == null || Sink == IntPtr.Zero)
            {
                return;
            }

            var Add = (delegate* unmanaged[Stdcall]<IntPtr, byte*, int, void>)Sink;
            fixed (byte* Bytes = Blob)
            {
                Add(Context, Bytes, Blob.Length);
            }
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Writes a script type's [Button] methods (method name + label + tooltip, flat) to a native
    /// sink. sink(ctx, bytes, len) is called once. Drives the inspector's clickable action buttons.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void GetScriptButtons(byte* ScriptClass, int ClassLength, IntPtr Sink, IntPtr Context)
    {
        try
        {
            byte[]? Blob = Scripts?.EntityScripts?.Buttons(Interop.GetString(ScriptClass, ClassLength));
            if (Blob == null || Sink == IntPtr.Zero)
            {
                return;
            }

            var Add = (delegate* unmanaged[Stdcall]<IntPtr, byte*, int, void>)Sink;
            fixed (byte* Bytes = Blob)
            {
                Add(Context, Bytes, Blob.Length);
            }
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Applies a recursive override-value blob (native-serialized FScriptPropertyOverrides) onto
    /// a live script instance via reflection. Supports nested structs + arrays.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void ApplyScriptProperties(IntPtr Handle, byte* Blob, int Length)
    {
        try
        {
            Scripts?.EntityScripts?.ApplyProperties(Handle, Blob, Length);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Resumes an Asset.LoadAsync continuation: Callback is the GCHandle to an Action&lt;IntPtr&gt;
    /// trampoline (captures the requested type), Object is the loaded CObject* (or zero). Freed here.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void InvokeAssetCallback(IntPtr Callback, IntPtr Object)
    {
        try
        {
            GCHandle Handle = GCHandle.FromIntPtr(Callback);
            Action<IntPtr>? Trampoline = Handle.Target as Action<IntPtr>;
            Handle.Free();
            Trampoline?.Invoke(Object);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Reports every loaded EntityScript type's full name to a native sink (for the editor's
    /// script-picker dropdown). sink(ctx, utf8Name, byteLen) is called once per type.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void EnumerateEntityScripts(IntPtr Sink, IntPtr Context)
    {
        try
        {
            EntityScriptRuntime? Runtime = Scripts?.EntityScripts;
            if (Runtime == null || Sink == IntPtr.Zero)
            {
                return;
            }

            var Add = (delegate* unmanaged[Stdcall]<IntPtr, byte*, int, void>)Sink;
            Span<byte> Scratch = stackalloc byte[256];
            foreach (string Name in Runtime.TypeNames)
            {
                Interop.FInteropString Encoded = new(Name, Scratch);
                try
                {
                    Add(Context, Encoded.Pointer, Encoded.Length);
                }
                finally
                {
                    Encoded.Free();
                }
            }
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    //~ EntitySystem bridge: a C# system the native stage scheduler ticks as a first-class system. Mirrors
    //  the EntityScript entries but one instance per WORLD (not per entity); the GCHandle is the FStageSlot
    //  Self the shared native shim forwards OnUpdate to.

    /// <summary>Reports every discovered EntitySystem to a native sink as (full name, stage, priority, declared
    /// write-ops tokens, declared read-ops tokens). Called once per type; see EntitySystemRuntime.Enumerate.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void EnumerateEntitySystems(IntPtr Sink, IntPtr Context)
    {
        try
        {
            Scripts?.EntitySystems?.Enumerate(Sink, Context);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>Instantiates an EntitySystem of the given full type name for a world; returns a strong
    /// GCHandle (as IntPtr) the native FStageSlot stores as Self, or IntPtr.Zero on failure.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static IntPtr CreateEntitySystem(byte* TypeName, int TypeNameLength, ulong World)
    {
        try
        {
            return Scripts?.EntitySystems?.Create(Interop.GetString(TypeName, TypeNameLength), World) ?? IntPtr.Zero;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return IntPtr.Zero;
        }
    }

    /// <summary>Ticks one EntitySystem instance: forwards to OnUpdate with the native FSystemContext*.</summary>
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void TickEntitySystem(IntPtr Handle, IntPtr SystemContext)
    {
        try
        {
            Scripts?.EntitySystems?.Tick(Handle, SystemContext);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void DestroyEntitySystem(IntPtr Handle)
    {
        try
        {
            Scripts?.EntitySystems?.Destroy(Handle);
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    // Native has already gathered + read every Scripts/*.cs across all mounts, bucketed per compilation unit
    // (game, each enabled plugin, the engine library) with each unit's dependency names. Each bucket becomes
    // one assembly in the shared collectible ALC. Replaces the live generation (compile new, then unload old).
    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static int LoadScripts(FSourceAssembly* Units, int Count)
    {
        try
        {
            if (Scripts == null)
            {
                return 1;
            }

            var List = new List<ScriptAssemblyUnit>(Count < 0 ? 0 : Count);
            for (int Index = 0; Index < Count; Index++)
            {
                ref FSourceAssembly Unit = ref Units[Index];

                string DepsJoined = Interop.GetString(Unit.Deps, Unit.DepsLength);
                string[] Deps = DepsJoined.Length == 0
                    ? Array.Empty<string>()
                    : DepsJoined.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

                var Sources = new List<(string, string)>(Unit.SourceCount < 0 ? 0 : Unit.SourceCount);
                for (int S = 0; S < Unit.SourceCount; S++)
                {
                    ref FSourceFile Source = ref Unit.Sources[S];
                    Sources.Add((Interop.GetString(Source.Path, Source.PathLength), Interop.GetString(Source.Text, Source.TextLength)));
                }

                string DllPath = Interop.GetString(Unit.DllPath, Unit.DllPathLength);
                List.Add(new ScriptAssemblyUnit
                {
                    Name = Interop.GetString(Unit.Name, Unit.NameLength),
                    Dependencies = Deps,
                    Sources = Sources,
                    DllPath = DllPath.Length == 0 ? null : DllPath,
                });
            }
            return Scripts.LoadOrReload(List) ? 0 : 4;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
            return 3;
        }
    }

    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void Tick()
    {
        try
        {
            Scripts?.Tick();
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    [ManagedExport]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    public static void Shutdown()
    {
        try
        {
            Scripts?.Shutdown();
            Scripts = null;
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }
}
