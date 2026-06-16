using System;

namespace LuminaSharp;

/// <summary>
/// Exposes a script field/property to the editor (and saves it), the C# analog of Lua's
/// <c>--@export(...)</c> and S&amp;Box's <c>[Property]</c>. The native side reflects these off the loaded
/// script type to drive the inspector and per-instance serialization.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false)]
public sealed class PropertyAttribute : Attribute
{
    /// <summary>Inspector group.</summary>
    public string? Category { get; set; }

    /// <summary>Hover help.</summary>
    public string? Tooltip { get; set; }

    /// <summary>Display label override (defaults to the member name).</summary>
    public string? Name { get; set; }

    /// <summary>Slider/drag lower bound (NaN = unbounded).</summary>
    public float Min { get; set; } = float.NaN;

    /// <summary>Slider/drag upper bound (NaN = unbounded).</summary>
    public float Max { get; set; } = float.NaN;

    /// <summary>Unit suffix shown after the value (e.g. "m/s").</summary>
    public string? Units { get; set; }

    /// <summary>For a string field: the asset class to pick (e.g. "CMaterial", "CStaticMesh"), making the
    /// inspector show a searchable asset picker that stores the chosen asset's virtual path. The script
    /// loads it with <c>Asset.Load&lt;T&gt;(field)</c>. The C# analog of Lua's --@export(AssetType="...").</summary>
    public string? AssetType { get; set; }

    /// <summary>Draw a color picker for a Vector3/Vector4 value instead of drag fields.</summary>
    public bool Color { get; set; }

    /// <summary>Draw a slider (needs Min+Max) for a numeric value instead of a drag field.</summary>
    public bool Slider { get; set; }

    public bool HasMin => !float.IsNaN(Min);
    public bool HasMax => !float.IsNaN(Max);
}

/// <summary>
/// Persists a field/property with the entity without exposing it in the inspector, the C# analog of
/// S&amp;Box's <c>[Serialize]</c>. (A <c>[Property]</c> field is serialized too; use this for
/// saved-but-hidden state.)
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false)]
public sealed class SerializeAttribute : Attribute
{
}

/// <summary>Marks a field/property as never serialized or shown, even if otherwise eligible.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false)]
public sealed class HideAttribute : Attribute
{
}
