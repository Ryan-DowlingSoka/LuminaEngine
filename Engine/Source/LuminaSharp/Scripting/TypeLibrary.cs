using System;
using System.Collections.Generic;
using System.Reflection;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// The managed reflection registry for one loaded script generation, the single source of truth the
/// rest of the binding layer queries (modeled on s&amp;box's TypeLibrary, right-sized for a binding
/// layer over a C++ ECS). Indexes the EntityScript types and resolves any type to a cached
/// <see cref="TypeDescription"/>; replaces the hand-rolled type/property dictionaries. Rebuilt wholesale
/// each (re)load, so there is no per-entry invalidation.
/// </summary>
internal sealed class TypeLibrary
{
    private readonly Dictionary<string, TypeDescription> EntityScripts = new();
    private readonly Dictionary<string, Type> EntitySystems = new();
    private readonly Dictionary<Type, TypeDescription> ByType = new();

    public TypeLibrary(IEnumerable<Type> Types)
    {
        foreach (Type Type in Types)
        {
            if (Type.IsAbstract || Type.FullName is not { } FullName)
            {
                continue;
            }
            if (typeof(EntityScript).IsAssignableFrom(Type))
            {
                EntityScripts[FullName] = Describe(Type);
            }
            else if (typeof(EntitySystem).IsAssignableFrom(Type)
                     && Type.GetCustomAttribute<EntitySystemAttribute>() != null)
            {
                EntitySystems[FullName] = Type;
            }
        }
    }

    /// <summary>Full type names of every EntityScript (for the editor's script-picker dropdown).</summary>
    public IReadOnlyCollection<string> EntityScriptTypeNames => EntityScripts.Keys;

    /// <summary>Every discovered EntitySystem type (carries [EntitySystem]); for the native scheduler.</summary>
    public IReadOnlyCollection<Type> EntitySystemTypes => EntitySystems.Values;

    /// <summary>An EntitySystem type by full name, or null if unknown.</summary>
    public Type? GetEntitySystem(string FullName)
    {
        return EntitySystems.TryGetValue(FullName, out Type? Type) ? Type : null;
    }

    /// <summary>The description for an EntityScript by full name, or null if unknown.</summary>
    public TypeDescription? GetEntityScript(string FullName)
    {
        return EntityScripts.TryGetValue(FullName, out TypeDescription? Description) ? Description : null;
    }

    /// <summary>Get-or-build the description for any type (used recursively for nested struct members).</summary>
    public TypeDescription Describe(Type Type)
    {
        if (ByType.TryGetValue(Type, out TypeDescription? Cached))
        {
            return Cached;
        }

        TypeDescription Description = new(Type);
        ByType[Type] = Description; // insert before building members so self/cyclic references resolve
        Description.Build(this);
        return Description;
    }

    /// <summary>
    /// Resolves a CLR type to its recursive serialized shape. Scalars/vectors/enums map directly;
    /// arrays and lists become <see cref="EScriptKind.Array"/>; any other struct/class becomes a
    /// <see cref="EScriptKind.NestedStruct"/> whose members recurse. Returns Nil for shapes we can't
    /// serialize. <paramref name="Depth"/> + <paramref name="Visiting"/> guard against cycles.
    /// </summary>
    public ScriptType ResolveType(Type Type, int Depth, HashSet<Type> Visiting)
    {
        if (Type == typeof(bool))
        {
            return new ScriptType { Kind = EScriptKind.Bool, Clr = Type };
        }
        if (Type.IsEnum || Type == typeof(int) || Type == typeof(uint) || Type == typeof(short) || Type == typeof(ushort)
            || Type == typeof(byte) || Type == typeof(sbyte) || Type == typeof(long) || Type == typeof(ulong))
        {
            return new ScriptType { Kind = EScriptKind.Int, Clr = Type };
        }
        if (Type == typeof(float) || Type == typeof(double))
        {
            return new ScriptType { Kind = EScriptKind.Double, Clr = Type };
        }
        if (Type == typeof(string))
        {
            return new ScriptType { Kind = EScriptKind.String, Clr = Type };
        }
        if (Type == typeof(FVector2))
        {
            return new ScriptType { Kind = EScriptKind.Vec2, Clr = Type };
        }
        if (Type == typeof(FVector3))
        {
            return new ScriptType { Kind = EScriptKind.Vec3, Clr = Type };
        }
        if (Type == typeof(FVector4))
        {
            return new ScriptType { Kind = EScriptKind.Vec4, Clr = Type };
        }

        // Asset-reference types serialize as a path string (drawn as an AssetType picker). Checked before
        // the generic struct fallback so they aren't mistaken for nested structs.
        if (Type == typeof(FSoftObjectPath))
        {
            return new ScriptType { Kind = EScriptKind.String, Clr = Type, IsAssetRef = true, AssetType = "" };
        }
        if (Type.IsGenericType)
        {
            Type Definition = Type.GetGenericTypeDefinition();
            if (Definition == typeof(TSoftObjectPtr<>) || Definition == typeof(TObjectPtr<>))
            {
                Type Target = Type.GetGenericArguments()[0];
                return new ScriptType { Kind = EScriptKind.String, Clr = Type, IsAssetRef = true, AssetType = Target.Name };
            }
        }

        if (TryGetElementType(Type, out Type? ElementType))
        {
            ScriptType Element = ResolveType(ElementType!, Depth + 1, Visiting);
            if (Element.Kind == EScriptKind.Nil)
            {
                return new ScriptType { Kind = EScriptKind.Nil, Clr = Type };
            }
            return new ScriptType { Kind = EScriptKind.Array, Clr = Type, Element = Element };
        }

        // Any other struct/class with serializable members becomes a nested struct. Guard depth + cycles.
        if ((Type.IsClass || (Type.IsValueType && !Type.IsPrimitive)) && Depth < 16 && Visiting.Add(Type))
        {
            try
            {
                List<ScriptProperty> Fields = BuildMembers(Type, false, Depth, Visiting);
                if (Fields.Count > 0)
                {
                    return new ScriptType { Kind = EScriptKind.NestedStruct, Clr = Type, Fields = Fields };
                }
            }
            finally
            {
                Visiting.Remove(Type);
            }
        }

        return new ScriptType { Kind = EScriptKind.Nil, Clr = Type };
    }

    /// <summary>
    /// Builds the serializable members of a type. At the top level (<paramref name="bTopLevel"/> true)
    /// only members carrying [Property] are exposed; inside a nested struct every public read/write
    /// field/property whose type resolves is auto-exposed (Unity-style).
    /// </summary>
    internal List<ScriptProperty> BuildMembers(Type Type, bool bTopLevel, int Depth, HashSet<Type> Visiting)
    {
        var Members = new List<ScriptProperty>();
        const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.FlattenHierarchy;

        foreach (FieldInfo Field in Type.GetFields(Flags))
        {
            PropertyAttribute? Meta = Field.GetCustomAttribute<PropertyAttribute>();
            if (bTopLevel)
            {
                if (Meta == null || Field.GetCustomAttribute<HideAttribute>() != null)
                {
                    continue;
                }
            }
            else if (!Field.IsPublic)
            {
                continue;
            }

            ScriptType Resolved = ResolveType(Field.FieldType, Depth + 1, Visiting);
            if (Resolved.Kind == EScriptKind.Nil)
            {
                continue;
            }

            Members.Add(new ScriptProperty
            {
                Name = Meta?.Name ?? Field.Name,
                Type = Resolved,
                Meta = Meta,
                Get = Field.GetValue,
                Set = Field.SetValue,
            });
        }

        foreach (PropertyInfo Property in Type.GetProperties(Flags))
        {
            if (!Property.CanRead || !Property.CanWrite || Property.GetIndexParameters().Length > 0)
            {
                continue;
            }

            PropertyAttribute? Meta = Property.GetCustomAttribute<PropertyAttribute>();
            if (bTopLevel)
            {
                if (Meta == null || Property.GetCustomAttribute<HideAttribute>() != null)
                {
                    continue;
                }
            }
            else if (!(Property.GetMethod?.IsPublic ?? false))
            {
                continue;
            }

            ScriptType Resolved = ResolveType(Property.PropertyType, Depth + 1, Visiting);
            if (Resolved.Kind == EScriptKind.Nil)
            {
                continue;
            }

            Members.Add(new ScriptProperty
            {
                Name = Meta?.Name ?? Property.Name,
                Type = Resolved,
                Meta = Meta,
                Get = Property.GetValue,
                Set = Property.SetValue,
            });
        }

        return Members;
    }

    private static bool TryGetElementType(Type Type, out Type? ElementType)
    {
        if (Type.IsArray && Type.GetArrayRank() == 1)
        {
            ElementType = Type.GetElementType();
            return ElementType != null;
        }
        if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(List<>))
        {
            ElementType = Type.GetGenericArguments()[0];
            return true;
        }
        ElementType = null;
        return false;
    }
}

/// <summary>
/// Cached, immutable-after-Build description of one script type: the recursive [Property] member set
/// and the precomputed collision-callback bitmask. The instance is always passed in at call time, one
/// description serves every entity carrying the type. Instantiation uses <see cref="Activator"/>
/// deliberately: a compiled Expression factory would JIT into the process-wide dynamic-methods assembly
/// referencing this (collectible) script ALC's constructor and pin it, so the ALC could never unload on
/// hot reload. Scripts spawn on attach (not per frame), so the Activator cost is irrelevant.
/// </summary>
internal sealed class TypeDescription
{
    private static readonly string[] CollisionCallbacks = { "OnContactBegin", "OnContactEnd", "OnOverlapBegin", "OnOverlapEnd" };

    public Type Type { get; }
    public IReadOnlyList<ScriptProperty> Properties { get; private set; } = Array.Empty<ScriptProperty>();
    public int CollisionCallbackFlags { get; private set; }

    public TypeDescription(Type Type)
    {
        this.Type = Type;
    }

    public void Build(TypeLibrary Library)
    {
        Properties = Library.BuildMembers(Type, true, 0, new HashSet<Type>());
        CollisionCallbackFlags = ComputeCollisionFlags(Type);
    }

    public object? Create()
    {
        if (Type.IsAbstract || Type.IsInterface)
        {
            return null;
        }

        try
        {
            return Activator.CreateInstance(Type);
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"Failed to instantiate script '{Type.FullName}': {Exception}");
            return null;
        }
    }

    private static int ComputeCollisionFlags(Type Type)
    {
        int Flags = 0;
        for (int Index = 0; Index < CollisionCallbacks.Length; Index++)
        {
            MethodInfo? Method = Type.GetMethod(
                CollisionCallbacks[Index],
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                null, new[] { typeof(Lumina.SCollisionEvent) }, null);
            if (Method != null && Method.DeclaringType != typeof(EntityScript))
            {
                Flags |= 1 << Index;
            }
        }

        // OnInput (bit 4): event-driven input listening. Different signature (InputEvent), checked separately.
        MethodInfo? Input = Type.GetMethod("OnInput",
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            null, new[] { typeof(Lumina.InputEvent) }, null);
        if (Input != null && Input.DeclaringType != typeof(EntityScript))
        {
            Flags |= 1 << 4;
        }
        return Flags;
    }
}
