using System;

namespace LuminaSharp;

/// <summary>
/// Exclude-filter markers for <see cref="EntityRegistry.View{T1}(Exclude)"/> and friends. Each carries the
/// resolved component-ops tokens of the excluded types, mirroring entt's <c>entt::exclude&lt;...&gt;</c>.
/// Construct with the static <c>Registry.Exclude&lt;...&gt;()</c> helpers (or <c>EntityRegistry.Exclude</c>).
/// </summary>
public readonly struct Exclude
{
    internal readonly IntPtr Token0;
    internal readonly IntPtr Token1;
    internal readonly IntPtr Token2;
    internal readonly int Count;

    internal Exclude(IntPtr Token0, IntPtr Token1, IntPtr Token2, int Count)
    {
        this.Token0 = Token0;
        this.Token1 = Token1;
        this.Token2 = Token2;
        this.Count = Count;
    }

    /// <summary>An empty exclude set (the default for an unfiltered view).</summary>
    public static readonly Exclude None = new(IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 0);
}

/// <summary>
/// Builds one fresh component wrapper per type for a single View iteration, via its generated
/// <c>(IntPtr)</c> ctor (handle 0). The View rebinds the wrapper's handle each step with
/// <see cref="NativeStruct.SetHandle"/>, so there is NO per-element allocation -- one wrapper services the
/// whole iteration. A fresh wrapper per Each/foreach call keeps nested and parallel iterations independent.
/// </summary>
internal static class ViewWrapper<T> where T : NativeStruct
{
    public static T New()
    {
        return Wrapper<T>.Create(IntPtr.Zero) ?? throw new InvalidOperationException(
            $"{typeof(T).Name} has no (IntPtr) ctor; cannot build a reusable View wrapper.");
    }
}
