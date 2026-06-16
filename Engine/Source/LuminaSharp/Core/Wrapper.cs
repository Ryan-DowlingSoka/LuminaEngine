using System;
using System.Linq.Expressions;
using System.Reflection;

namespace LuminaSharp;

/// <summary>
/// Builds (once, then caches) a fast constructor for a generated wrapper type from its internal
/// <c>(IntPtr)</c> ctor, so the registry can turn a native component pointer into its typed wrapper
/// without per-call reflection.
/// </summary>
internal static class Wrapper<T> where T : class
{
    public static readonly Func<IntPtr, T?> Create = Build();

    private static Func<IntPtr, T?> Build()
    {
        ConstructorInfo? Constructor = typeof(T).GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
            null, new[] { typeof(IntPtr) }, null);
        if (Constructor == null)
        {
            return Handle => null;
        }

        ParameterExpression Parameter = Expression.Parameter(typeof(IntPtr), "handle");
        return Expression.Lambda<Func<IntPtr, T?>>(Expression.New(Constructor, Parameter), Parameter).Compile();
    }
}
