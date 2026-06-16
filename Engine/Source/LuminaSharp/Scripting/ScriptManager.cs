using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace LuminaSharp;

/// <summary>
/// Owns one loaded generation of user C# scripts: compiles the sources into a collectible
/// AssemblyLoadContext, builds the <see cref="TypeLibrary"/> from the loaded types, and exposes the
/// <see cref="EntityScriptRuntime"/> the native entry points dispatch into. Reload compiles a fresh
/// generation and, only if that succeeds, frees the previous generation's live handles and unloads its
/// ALC (no engine restart). There is no archetype/dictionary indirection, the world's native ECS
/// system drives every per-instance lifecycle call.
/// </summary>
internal sealed class ScriptManager
{
    private ScriptLoadContext? Context;

    /// <summary>The runtime for the current generation, or null when no scripts are loaded.</summary>
    public EntityScriptRuntime? EntityScripts { get; private set; }

    /// <summary>The EntitySystem runtime for the current generation, or null when no scripts are loaded.</summary>
    public EntitySystemRuntime? EntitySystems { get; private set; }

    /// <summary>Bumps on every successful (re)load; the native side rebinds entity scripts when it changes.</summary>
    public int Generation { get; private set; }

    public bool LoadOrReload(IReadOnlyList<(string Path, string Text)> Sources)
    {
        // Compile the new generation first; never tear down working scripts for a broken edit.
        byte[]? PeBytes = null;
        if (Sources.Count > 0)
        {
            PeBytes = ScriptCompiler.Compile($"GameScripts.Gen{Generation + 1}", Sources);
            if (PeBytes == null)
            {
                Native.Log(ELogLevel.Error, "Script reload aborted: compilation failed; keeping current scripts.");
                return false;
            }
        }

        UnloadCurrent();

        if (PeBytes == null)
        {
            Native.Log(ELogLevel.Info, "No C# scripts found.");
            return true;
        }

        Generation++;
        var NewContext = new ScriptLoadContext($"GameScripts.Gen{Generation}");
        // LoadFromStream (not LoadFromAssemblyPath) so no file is locked and reload stays clean.
        Assembly LoadedAssembly = NewContext.LoadFromStream(new MemoryStream(PeBytes));
        Context = NewContext;

        Type[] Types = LoadedAssembly.GetTypes();
        var Library = new TypeLibrary(Types);
        EntityScripts = new EntityScriptRuntime(Library);
        EntitySystems = new EntitySystemRuntime(Library);

        Native.Log(ELogLevel.Info,
            $"Loaded C# scripts [generation {Generation}]: {Types.Length} type(s), " +
            $"{Library.EntityScriptTypeNames.Count} EntityScript(s), " +
            $"{Library.EntitySystemTypes.Count} EntitySystem(s).");
        return true;
    }

    // Global per-frame pump. Per-world ticking is driven by the native ECS system (UpdateScripts), so
    // nothing happens here today; kept as the single managed frame hook for future global work.
    public void Tick()
    {
    }

    public void Shutdown()
    {
        UnloadCurrent();
    }

    private void UnloadCurrent()
    {
        if (Context == null)
        {
            return;
        }

        // Free every live script + system GCHandle BEFORE unloading: a strong handle is a GC root that
        // would otherwise pin the old generation and stop the collectible ALC from unloading.
        EntityScripts?.FreeAll();
        EntityScripts = null;
        EntitySystems?.FreeAll();
        EntitySystems = null;

        // Confine the only strong reference to the ALC inside a method that fully returns before we
        // collect: a collectible ALC won't unload while any caller frame (even a JIT-spilled temp under
        // tier-0) still holds it. The GC loop then runs with no root.
        WeakReference Weak = UnloadContextLocked();

        for (int Index = 0; Weak.IsAlive && Index < 10; Index++)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }

        Native.Log(Weak.IsAlive ? ELogLevel.Warn : ELogLevel.Info,
            Weak.IsAlive
                ? "Previous script ALC did NOT unload (a managed reference leaked)."
                : "Previous script ALC unloaded cleanly.");
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private WeakReference UnloadContextLocked()
    {
        var Weak = new WeakReference(Context);
        Context!.Unload();
        Context = null;
        return Weak;
    }
}
