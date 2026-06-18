namespace LuminaSharp;

/// <summary>
/// Lightweight gameplay CPU profiler. Wrap any gameplay work in a scope and it shows up,  aggregated by
/// name, with call count and inclusive/self ms,  in the editor's Gameplay Profiler tool. Near-zero cost
/// when nobody is recording (<see cref="Enabled"/> is false). Game thread only.
///
/// Entity script and system <c>OnUpdate</c> are auto-profiled by their type name, so per-script timings
/// appear with no work. Use <see cref="Sample"/> to break a hot method into sub-scopes:
/// <code>using (Profiler.Sample("Perception")) { ... }</code>
/// </summary>
public static partial class Profiler
{
    /// <summary>True while the editor Gameplay Profiler is open (or recording is otherwise enabled).</summary>
    public static bool Enabled => IsEnabledRaw() != 0;

    /// <summary>Open a named scope. Pair with <see cref="End"/>; prefer <see cref="Sample"/> for exception safety.</summary>
    public static void Begin(string Name) => BeginRaw(Name);

    /// <summary>Close the most recently opened scope.</summary>
    public static void End() => EndRaw();

    /// <summary>
    /// A <c>using</c>-scoped timer that closes when the block exits (including on exceptions):
    /// <c>using (Profiler.Sample("Pathfind")) { ... }</c>.
    /// </summary>
    public static Scope Sample(string Name) => new Scope(Name);

    /// <summary>The disposable returned by <see cref="Sample"/>. Stack-only (a ref struct); do not store it.</summary>
    public readonly ref struct Scope
    {
        public Scope(string Name) => BeginRaw(Name);
        public void Dispose() => EndRaw();
    }

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayProfiler_Begin")]
    private static partial void BeginRaw(string Name);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayProfiler_End")]
    private static partial void EndRaw();
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayProfiler_IsEnabled")]
    private static partial int IsEnabledRaw();
}
