using System;
using System.Collections.Generic;

namespace LuminaSharp;

/// <summary>
/// The native->managed export table, resolved by name instead of a hand-mirrored struct. Native asks for a
/// managed function pointer by name (see <c>DotNet::ResolveManagedExport</c>); this is the lookup it hits.
/// Two tiers:
/// <list type="bullet">
/// <item><b>Engine exports</b> live in LuminaSharp.dll (non-collectible) and are registered once at bootstrap
/// by the generated <c>ManagedExportTable.RegisterEngineExports</c>. Their pointers are valid for the whole
/// process, so native caches them.</item>
/// <item><b>Script exports</b> are defined by plugin/game C# in the collectible script ALC; a plugin registers
/// one from a <c>[ModuleInitializer]</c> via <see cref="Register"/>. Their pointers dangle once a generation
/// unloads, so the whole tier is cleared on every reload and native must re-resolve after a generation bump
/// (watch <c>GetScriptGeneration</c>).</item>
/// </list>
/// </summary>
public static class ManagedExportRegistry
{
    private static readonly Dictionary<string, IntPtr> EngineExports = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, IntPtr> ScriptExports = new(StringComparer.Ordinal);

    /// <summary>Registers a permanent engine export (LuminaSharp.dll). Called by generated bootstrap code.</summary>
    public static void RegisterEngine(string Name, IntPtr Pointer)
    {
        EngineExports[Name] = Pointer;
    }

    /// <summary>Registers a script/plugin export. Call from a <c>[ModuleInitializer]</c> in a script assembly:
    /// the pointer must be a <c>delegate* unmanaged</c> to a <c>[UnmanagedCallersOnly]</c> static method. The
    /// entry is dropped on the next reload (the pointer would dangle), so native re-resolves per generation.</summary>
    public static void Register(string Name, IntPtr Pointer)
    {
        ScriptExports[Name] = Pointer;
    }

    /// <summary>Drops every script-tier export. Called when a generation unloads (its pointers go stale); the
    /// next generation's module initializers repopulate it. Engine exports are untouched.</summary>
    public static void ClearScriptExports()
    {
        ScriptExports.Clear();
    }

    /// <summary>Resolves an export by name to its function pointer, or IntPtr.Zero if unknown. Engine exports
    /// win over script exports so a plugin can't shadow a core entry.</summary>
    public static IntPtr Resolve(string Name)
    {
        if (EngineExports.TryGetValue(Name, out IntPtr Engine))
        {
            return Engine;
        }
        return ScriptExports.TryGetValue(Name, out IntPtr Script) ? Script : IntPtr.Zero;
    }
}
