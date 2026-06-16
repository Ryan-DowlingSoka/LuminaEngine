using System;
using System.Collections;
using System.Collections.Generic;

namespace LuminaSharp;

/// <summary>
/// A read-only view over a native TVector&lt;T&gt; member, surfaced by the generated bindings. Backed by
/// a count and a per-index projection (which reads the element through a native thunk and, for
/// object/struct elements, wraps it). The view is a snapshot of the count at construction; it does not
/// own the native storage, so don't retain it past the owner's lifetime.
/// </summary>
public readonly struct NativeReadOnlyList<T> : IReadOnlyList<T>
{
    private readonly int Length;
    private readonly Func<int, T> Getter;

    public NativeReadOnlyList(int Count, Func<int, T> Get)
    {
        Length = Count < 0 ? 0 : Count;
        Getter = Get;
    }

    public int Count => Length;

    public T this[int Index]
    {
        get
        {
            if ((uint)Index >= (uint)Length)
            {
                throw new ArgumentOutOfRangeException(nameof(Index));
            }
            return Getter(Index);
        }
    }

    public IEnumerator<T> GetEnumerator()
    {
        for (int Index = 0; Index < Length; Index++)
        {
            yield return Getter(Index);
        }
    }

    IEnumerator IEnumerable.GetEnumerator()
    {
        return GetEnumerator();
    }
}
