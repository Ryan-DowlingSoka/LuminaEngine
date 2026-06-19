using System;
using System.Collections.Concurrent;
using System.Reflection;

namespace LuminaSharp;

/// <summary>
/// Cached, fast property get/set delegates. Building an open-instance delegate once and reusing it is far
/// cheaper than calling <see cref="PropertyInfo.GetValue(object)"/> / <see cref="PropertyInfo.SetValue(object, object)"/>
/// repeatedly (those re-walk reflection metadata every call). Value-type properties still box on the
/// boundary, but the per-call reflection cost is gone. General-purpose: anything reflecting over properties
/// (UI data binding, serialization, the property system) can share this instead of rolling its own.
/// </summary>
public static class PropertyAccessor
{
    private static readonly ConcurrentDictionary<PropertyInfo, Func<object, object?>> Getters = new();
    private static readonly ConcurrentDictionary<PropertyInfo, Action<object, object?>?> Setters = new();

    /// <summary>A cached getter for <paramref name="Property"/>. Throws if the property has no getter.</summary>
    public static Func<object, object?> Getter(PropertyInfo Property)
    {
        return Getters.GetOrAdd(Property, static P =>
        {
            MethodInfo? Get = P.GetGetMethod(true);
            if (Get == null)
            {
                throw new ArgumentException($"Property '{P.DeclaringType?.Name}.{P.Name}' has no getter.");
            }
            MethodInfo Builder = typeof(PropertyAccessor)
                .GetMethod(nameof(BuildGetter), BindingFlags.NonPublic | BindingFlags.Static)!
                .MakeGenericMethod(P.DeclaringType!, P.PropertyType);
            return (Func<object, object?>)Builder.Invoke(null, new object[] { Get })!;
        });
    }

    /// <summary>A cached setter for <paramref name="Property"/>, or null if it is read-only.</summary>
    public static Action<object, object?>? Setter(PropertyInfo Property)
    {
        return Setters.GetOrAdd(Property, static P =>
        {
            MethodInfo? Set = P.GetSetMethod(true);
            if (Set == null)
            {
                return null;
            }
            MethodInfo Builder = typeof(PropertyAccessor)
                .GetMethod(nameof(BuildSetter), BindingFlags.NonPublic | BindingFlags.Static)!
                .MakeGenericMethod(P.DeclaringType!, P.PropertyType);
            return (Action<object, object?>?)Builder.Invoke(null, new object[] { Set });
        });
    }

    private static Func<object, object?> BuildGetter<TTarget, TValue>(MethodInfo Get)
    {
        var Typed = (Func<TTarget, TValue>)Delegate.CreateDelegate(typeof(Func<TTarget, TValue>), Get);
        return Target => Typed((TTarget)Target);
    }

    private static Action<object, object?> BuildSetter<TTarget, TValue>(MethodInfo Set)
    {
        var Typed = (Action<TTarget, TValue>)Delegate.CreateDelegate(typeof(Action<TTarget, TValue>), Set);
        return (Target, Value) => Typed((TTarget)Target, (TValue)Value!);
    }
}
