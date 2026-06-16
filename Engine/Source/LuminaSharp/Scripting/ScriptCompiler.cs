using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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

    /// <summary>Compiles all sources into PE bytes, or returns null on error (diagnostics logged).</summary>
    public static byte[]? Compile(string AssemblyName, IReadOnlyList<(string Path, string Text)> Sources)
    {
        var ParseOptions = new CSharpParseOptions(LanguageVersion.Latest);
        SyntaxTree[] Trees = Sources
            .Select(Source => CSharpSyntaxTree.ParseText(SourceText.From(Source.Text), ParseOptions, path: Source.Path))
            .ToArray();

        var Compilation = CSharpCompilation.Create(
            AssemblyName,
            Trees,
            References,
            new CSharpCompilationOptions(
                OutputKind.DynamicallyLinkedLibrary,
                optimizationLevel: OptimizationLevel.Debug,
                allowUnsafe: true,
                // Enable the nullable annotations context so scripts can use `T?` without CS8632.
                nullableContextOptions: NullableContextOptions.Enable));

        using var PeStream = new MemoryStream();
        EmitResult Result = Compilation.Emit(PeStream);

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
}
