using System.Reflection;
using System.Runtime.Loader;

namespace LuminaSharp;

/// <summary>
/// Collectible load context for one generation of user scripts. Returning null from Load() defers
/// every referenced assembly (LuminaSharp, the framework) to the Default context so their types unify;
/// only the compiled script assembly lives here, which is what lets Unload() reclaim the whole
/// generation on hot reload.
/// </summary>
internal sealed class ScriptLoadContext : AssemblyLoadContext
{
    // The engine assembly's simple name + the context it actually lives in. hostfxr loads LuminaSharp
    // into its own component context (NOT Default), so a script that references EntityScript must be
    // pointed at that exact assembly, otherwise the Default fallback can't find it. Framework
    // assemblies (System.*) still resolve from Default via null.
    private static readonly Assembly EngineAssembly = typeof(Host).Assembly;
    private static readonly string EngineAssemblyName = EngineAssembly.GetName().Name!;

    public ScriptLoadContext(string Name) : base(Name, isCollectible: true)
    {
    }

    protected override Assembly? Load(AssemblyName Name)
    {
        if (Name.Name == EngineAssemblyName)
        {
            return EngineAssembly;
        }
        return null;
    }
}
