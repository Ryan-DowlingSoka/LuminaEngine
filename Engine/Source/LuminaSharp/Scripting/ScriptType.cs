using System;
using System.Collections.Generic;

namespace LuminaSharp;

/// <summary>
/// Discriminator for a script property's serialized shape. Mirrors Lumina::Scripting::EScriptExportKind
/// exactly (same integer values) so the native schema/value codec and editor reuse one model.
/// </summary>
public enum EScriptKind : byte
{
    Nil = 0,
    Bool = 1,
    Int = 2,
    Double = 3,
    String = 4,
    Vec2 = 5,
    Vec3 = 6,
    Vec4 = 7,
    Array = 8,
    NestedStruct = 9,
    UnknownUserdata = 10,
}

/// <summary>
/// The resolved, recursive shape of a script property's type. Scalars carry only <see cref="Kind"/>;
/// an <see cref="EScriptKind.Array"/> carries its <see cref="Element"/> type; a
/// <see cref="EScriptKind.NestedStruct"/> carries its member <see cref="Fields"/> (the nested type's
/// serializable members). Resolved once per type by the <see cref="TypeLibrary"/> and shared.
/// </summary>
public sealed class ScriptType
{
    public EScriptKind Kind { get; init; } = EScriptKind.Nil;

    /// <summary>The CLR type this describes (used for value coercion + nested instance creation).</summary>
    public Type Clr { get; init; } = typeof(object);

    /// <summary>Element shape when <see cref="Kind"/> is <see cref="EScriptKind.Array"/>.</summary>
    public ScriptType? Element { get; init; }

    /// <summary>Member shapes when <see cref="Kind"/> is <see cref="EScriptKind.NestedStruct"/>.</summary>
    public IReadOnlyList<ScriptProperty>? Fields { get; init; }

    /// <summary>True for an asset-reference type (FSoftObjectPath / TSoftObjectPtr&lt;T&gt; / TObjectPtr&lt;T&gt;):
    /// the wire <see cref="Kind"/> is <see cref="EScriptKind.String"/> (the path), but the serializer
    /// round-trips it through <see cref="IAssetRef"/> instead of as a plain string.</summary>
    public bool IsAssetRef { get; init; }

    /// <summary>For an asset-reference type, the asset class to pick (e.g. "CMaterial"); "" = any.</summary>
    public string? AssetType { get; init; }
}

/// <summary>
/// One serializable member of a script type: its name, accessors (resolved once), recursive
/// <see cref="Type"/> shape, and optional editor metadata (only top-level [Property] members carry
/// meta; members of a nested struct are auto-exposed without it).
/// </summary>
public sealed class ScriptProperty
{
    public string Name { get; init; } = "";
    public ScriptType Type { get; init; } = new();
    public PropertyAttribute? Meta { get; init; }
    public Func<object, object?> Get { get; init; } = Instance => null;
    public Action<object, object?> Set { get; init; } = (Instance, Value) => { };
}

/// <summary>One [Button] method: a parameterless instance method surfaced as an inspector button. The
/// method is invoked by name through the generic managed-invoke path, so no compiled invoker is cached.</summary>
public sealed class ScriptButton
{
    /// <summary>The reflected method name (the invoke key).</summary>
    public string Method { get; init; } = "";

    /// <summary>Button text shown in the inspector.</summary>
    public string Label { get; init; } = "";

    /// <summary>Optional hover help.</summary>
    public string Tooltip { get; init; } = "";
}
