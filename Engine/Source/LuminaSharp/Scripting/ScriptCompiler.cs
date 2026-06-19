using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;

namespace LuminaSharp;

/// <summary>
/// Compiles loose C# script sources into one in-memory assembly with Roslyn. No csproj required:
/// scripts reference the framework (the TPA set) plus this engine assembly.
/// </summary>
internal static class ScriptCompiler
{
    private static MetadataReference[]? CachedReferences;

    private static MetadataReference[] References
    {
        get
        {
            if (CachedReferences != null)
            {
                return CachedReferences;
            }

            var Built = new List<MetadataReference>();

            // Every framework assembly the host resolved (BCL).
            string Tpa = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string ?? string.Empty;
            foreach (string EntryPath in Tpa.Split(Path.PathSeparator))
            {
                if (EntryPath.Length == 0)
                {
                    continue;
                }
                try
                {
                    Built.Add(MetadataReference.CreateFromFile(EntryPath));
                }
                catch
                {
                    // Skip unreadable entries.
                }
            }

            // The engine API assembly (LuminaSharp) so scripts can reference its archetype base classes
            // + generated bindings.
            string SelfPath = typeof(Host).Assembly.Location;
            if (!string.IsNullOrEmpty(SelfPath) && File.Exists(SelfPath))
            {
                Built.Add(MetadataReference.CreateFromFile(SelfPath));
            }

            CachedReferences = Built.ToArray();
            return CachedReferences;
        }
    }

    /// <summary>Compiles all sources into PE bytes, or returns null on error (diagnostics logged).
    /// <paramref name="ExtraReferences"/> are the emitted images of the unit's dependency assemblies, so a
    /// dependent plugin compiles against the exact metadata of the plugins it builds on.</summary>
    public static byte[]? Compile(string AssemblyName, IReadOnlyList<(string Path, string Text)> Sources,
        IReadOnlyList<MetadataReference>? ExtraReferences = null)
    {
        var ParseOptions = new CSharpParseOptions(LanguageVersion.Latest);
        SyntaxTree[] Trees = Sources
            .Select(Source => CSharpSyntaxTree.ParseText(SourceText.From(Source.Text), ParseOptions, path: Source.Path))
            .ToArray();

        MetadataReference[] AllReferences = References;
        if (ExtraReferences != null && ExtraReferences.Count > 0)
        {
            AllReferences = References.Concat(ExtraReferences).ToArray();
        }

        var Compilation = CSharpCompilation.Create(
            AssemblyName,
            Trees,
            AllReferences,
            new CSharpCompilationOptions(
                OutputKind.DynamicallyLinkedLibrary,
                optimizationLevel: OptimizationLevel.Release,
                allowUnsafe: true,
                nullableContextOptions: NullableContextOptions.Enable));

        Compilation Compiled = Compilation;
        ImmutableArray<ISourceGenerator> Generators = SourceGenerators;
        if (Generators.Length > 0)
        {
            GeneratorDriver Driver = CSharpGeneratorDriver.Create(Generators);
            Driver = Driver.RunGeneratorsAndUpdateCompilation(Compilation, out Compilation Updated, out ImmutableArray<Diagnostic> GeneratorDiagnostics);
            Compiled = Updated;
            foreach (Diagnostic Diagnostic in GeneratorDiagnostics)
            {
                if (Diagnostic.Severity == DiagnosticSeverity.Error)
                {
                    Native.Log(ELogLevel.Error, "C# generator: " + Diagnostic);
                }
            }
        }

        using var PeStream = new MemoryStream();
        EmitResult Result = Compiled.Emit(PeStream);

        bool bHadError = false;
        foreach (Diagnostic Diagnostic in Result.Diagnostics)
        {
            if (Diagnostic.Severity == DiagnosticSeverity.Error)
            {
                bHadError = true;
                Native.Log(ELogLevel.Error, "C# compile: " + Diagnostic);
            }
            else if (Diagnostic.Severity == DiagnosticSeverity.Warning)
            {
                Native.Log(ELogLevel.Warn, "C# compile: " + Diagnostic);
            }
        }

        if (Result.Success && !bHadError)
        {
            return PeStream.ToArray();
        }
        return null;
    }

    private static ImmutableArray<ISourceGenerator>? CachedGenerators;

    // The source generators to run on every script compile, loaded once. Only the [NativeCall] glue
    // generator: the ManagedExport generator is engine-only (it emits the engine's RegisterEngineExports and
    // would collide if run on a script assembly).
    private static ImmutableArray<ISourceGenerator> SourceGenerators => CachedGenerators ??= LoadGenerators();

    private static ImmutableArray<ISourceGenerator> LoadGenerators()
    {
        try
        {
            string Directory = Path.GetDirectoryName(typeof(Host).Assembly.Location) ?? string.Empty;
            string GeneratorPath = Path.Combine(Directory, "LuminaSharp.Generators.dll");
            if (!File.Exists(GeneratorPath))
            {
                Native.Log(ELogLevel.Warn,
                    $"Source generator not found at '{GeneratorPath}'; routed [NativeCall] bindings won't compile in scripts.");
                return ImmutableArray<ISourceGenerator>.Empty;
            }

            // Load into LuminaSharp's OWN ALC (not Default), so the generator's Microsoft.CodeAnalysis binds
            // to the exact instance LuminaSharp uses, otherwise IIncrementalGenerator has a different type
            // identity and nothing matches.
            AssemblyLoadContext Context = AssemblyLoadContext.GetLoadContext(typeof(Host).Assembly) ?? AssemblyLoadContext.Default;
            Assembly GeneratorAssembly = Context.LoadFromAssemblyPath(GeneratorPath);
            Type[] AllTypes;
            try
            {
                AllTypes = GeneratorAssembly.GetTypes();
            }
            catch (ReflectionTypeLoadException Exception)
            {
                Native.Log(ELogLevel.Error, $"Generator GetTypes failed: {Exception.LoaderExceptions.FirstOrDefault()?.Message}");
                AllTypes = Exception.Types.Where(T => T != null).ToArray()!;
            }

            var Found = new List<ISourceGenerator>();
            foreach (Type Type in AllTypes)
            {
                if (Type.Name == "NativeCallGenerator"
                    && typeof(IIncrementalGenerator).IsAssignableFrom(Type) && !Type.IsAbstract
                    && Activator.CreateInstance(Type) is IIncrementalGenerator Generator)
                {
                    Found.Add(Generator.AsSourceGenerator());
                }
            }
            return Found.ToImmutableArray();
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"Failed to load script source generators: {Exception.Message}");
            return ImmutableArray<ISourceGenerator>.Empty;
        }
    }
}
