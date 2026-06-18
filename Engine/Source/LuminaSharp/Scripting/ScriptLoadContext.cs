using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace LuminaSharp;

/// <summary>
/// Collectible load context for one generation of user scripts. Returning null from Load() defers
/// every referenced assembly (LuminaSharp, the framework) to the Default context so their types unify;
/// only the compiled script assemblies live here, which is what lets Unload() reclaim the whole
/// generation on hot reload.
///
/// A generation is made of MANY assemblies (one per plugin/game/engine unit) loaded into this single
/// context. Cross-unit references resolve here: each loaded unit is registered by its simple name and
/// returned from <see cref="Load"/>, so a dependent unit's reference to a dependency unit binds within
/// the ALC (and unloads with it) rather than escaping to the Default context.
/// </summary>
internal sealed class ScriptLoadContext : AssemblyLoadContext
{
    // The engine assembly's simple name + the context it actually lives in. hostfxr loads LuminaSharp
    // into its own component context (NOT Default), so a script that references EntityScript must be
    // pointed at that exact assembly, otherwise the Default fallback can't find it. Framework
    // assemblies (System.*) still resolve from Default via null.
    private static readonly Assembly EngineAssembly = typeof(Host).Assembly;
    private static readonly string EngineAssemblyName = EngineAssembly.GetName().Name!;

    // Script units loaded into THIS generation, keyed by the simple name compilation produced (the unit
    // label). Lets a dependent unit resolve a sibling dependency to the in-ALC assembly, not disk.
    private readonly Dictionary<string, Assembly> ScriptAssemblies = new(StringComparer.OrdinalIgnoreCase);

    public ScriptLoadContext(string Name) : base(Name, isCollectible: true)
    {
    }

    /// <summary>Loads one unit's PE image into this context and registers it under <paramref name="UnitName"/>
    /// so sibling units that reference it resolve here. The image is copied out of the caller's buffer.</summary>
    public Assembly LoadScriptAssembly(string UnitName, byte[] PeBytes)
    {
        Assembly Loaded = LoadFromStream(new MemoryStream(PeBytes));
        ScriptAssemblies[UnitName] = Loaded;
        return Loaded;
    }

    protected override Assembly? Load(AssemblyName Name)
    {
        if (Name.Name == EngineAssemblyName)
        {
            return EngineAssembly;
        }
        if (Name.Name is { } Simple && ScriptAssemblies.TryGetValue(Simple, out Assembly? Sibling))
        {
            return Sibling;
        }
        return null;
    }
}
