using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// The recursive blob codec bridging managed script reflection and the native script schema/value
/// model (Lumina::Scripting::FScriptExportSchema / FScriptPropertyValue, which is already nesting-complete).
/// One self-describing value encoding (a leading kind byte) is used both for the schema's default
/// values and for the per-instance overrides, so nested structs and arrays round-trip uniformly. The
/// managed side WRITES the schema (+ defaults) and READS overrides onto a live instance; the native
/// side does the mirror.
/// </summary>
internal static class Serializer
{
    // ---- Schema (managed -> native): types + meta + default values, recursive ----

    public static byte[] WriteSchema(TypeDescription Description)
    {
        object? Defaults = Description.Create();

        using var Stream = new MemoryStream();
        using var Writer = new BinaryWriter(Stream, Encoding.UTF8, leaveOpen: true);

        Writer.Write(Description.Properties.Count);
        foreach (ScriptProperty Property in Description.Properties)
        {
            WriteString(Writer, Property.Name);
            WriteMeta(Writer, Property.Meta, Property.Type.AssetType);
            WriteType(Writer, Property.Type);
            WriteValue(Writer, Property.Type, Defaults != null ? Property.Get(Defaults) : null);
        }

        Writer.Flush();
        return Stream.ToArray();
    }

    // ---- Buttons (managed -> native): [Button] method name + label + tooltip, flat ----

    public static byte[] WriteButtons(TypeDescription Description)
    {
        using var Stream = new MemoryStream();
        using var Writer = new BinaryWriter(Stream, Encoding.UTF8, leaveOpen: true);

        Writer.Write(Description.Buttons.Count);
        foreach (ScriptButton Button in Description.Buttons)
        {
            WriteString(Writer, Button.Method);
            WriteString(Writer, Button.Label);
            WriteString(Writer, Button.Tooltip);
        }

        Writer.Flush();
        return Stream.ToArray();
    }

    private static void WriteMeta(BinaryWriter Writer, PropertyAttribute? Meta, string? DerivedAssetType)
    {
        WriteString(Writer, Meta?.Category ?? "");
        WriteString(Writer, Meta?.Tooltip ?? "");
        WriteString(Writer, Meta?.Units ?? "");

        bool bHasMin = Meta?.HasMin ?? false;
        Writer.Write((byte)(bHasMin ? 1 : 0));
        if (bHasMin)
        {
            Writer.Write((double)Meta!.Min);
        }

        bool bHasMax = Meta?.HasMax ?? false;
        Writer.Write((byte)(bHasMax ? 1 : 0));
        if (bHasMax)
        {
            Writer.Write((double)Meta!.Max);
        }

        // Reference + widget hints (parsed in lockstep by DotNetHost GatherScriptSchema). A typed
        // asset-reference field's class wins; else the explicit [Property(AssetType=...)] on a string.
        string AssetType = !string.IsNullOrEmpty(DerivedAssetType) ? DerivedAssetType! : (Meta?.AssetType ?? "");
        WriteString(Writer, AssetType);
        Writer.Write((byte)((Meta?.Color ?? false) ? 1 : 0));
        Writer.Write((byte)((Meta?.Slider ?? false) ? 1 : 0));
    }

    private static void WriteType(BinaryWriter Writer, ScriptType Type)
    {
        Writer.Write((byte)Type.Kind);
        if (Type.Kind == EScriptKind.Array && Type.Element != null)
        {
            WriteType(Writer, Type.Element);
        }
        else if (Type.Kind == EScriptKind.NestedStruct && Type.Fields != null)
        {
            Writer.Write(Type.Fields.Count);
            foreach (ScriptProperty Field in Type.Fields)
            {
                WriteString(Writer, Field.Name);
                WriteType(Writer, Field.Type);
            }
        }
    }

    private static void WriteValue(BinaryWriter Writer, ScriptType Type, object? Value)
    {
        // Asset references serialize as a path string (wire kind String) via IAssetRef.
        if (Type.IsAssetRef)
        {
            Writer.Write((byte)EScriptKind.String);
            WriteString(Writer, Value is IAssetRef Reference ? Reference.GetPath() : "");
            return;
        }

        Writer.Write((byte)Type.Kind);
        switch (Type.Kind)
        {
            case EScriptKind.Bool:
            {
                Writer.Write((byte)(Value is bool Bool && Bool ? 1 : 0));
                break;
            }
            case EScriptKind.Int:
            {
                Writer.Write(Value != null ? Convert.ToInt64(Value) : 0L);
                break;
            }
            case EScriptKind.Double:
            {
                Writer.Write(Value != null ? Convert.ToDouble(Value) : 0.0);
                break;
            }
            case EScriptKind.String:
            {
                WriteString(Writer, Value as string ?? "");
                break;
            }
            case EScriptKind.Vec2:
            {
                FVector2 V = Value is FVector2 Typed ? Typed : default;
                Writer.Write(V.X);
                Writer.Write(V.Y);
                break;
            }
            case EScriptKind.Vec3:
            {
                FVector3 V = Value is FVector3 Typed ? Typed : default;
                Writer.Write(V.X);
                Writer.Write(V.Y);
                Writer.Write(V.Z);
                break;
            }
            case EScriptKind.Vec4:
            {
                FVector4 V = Value is FVector4 Typed ? Typed : default;
                Writer.Write(V.X);
                Writer.Write(V.Y);
                Writer.Write(V.Z);
                Writer.Write(V.W);
                break;
            }
            case EScriptKind.Array:
            {
                WriteArray(Writer, Type, Value);
                break;
            }
            case EScriptKind.NestedStruct:
            {
                WriteNested(Writer, Type, Value);
                break;
            }
        }
    }

    private static void WriteArray(BinaryWriter Writer, ScriptType Type, object? Value)
    {
        ScriptType Element = Type.Element ?? new ScriptType();
        if (Value is IEnumerable Enumerable && Type.Element != null)
        {
            var Items = new List<object?>();
            foreach (object? Item in Enumerable)
            {
                Items.Add(Item);
            }
            Writer.Write(Items.Count);
            foreach (object? Item in Items)
            {
                WriteValue(Writer, Element, Item);
            }
        }
        else
        {
            Writer.Write(0);
        }
    }

    private static void WriteNested(BinaryWriter Writer, ScriptType Type, object? Value)
    {
        IReadOnlyList<ScriptProperty> Fields = Type.Fields ?? Array.Empty<ScriptProperty>();
        Writer.Write(Fields.Count);
        foreach (ScriptProperty Field in Fields)
        {
            WriteString(Writer, Field.Name);
            WriteValue(Writer, Field.Type, Value != null ? Field.Get(Value) : null);
        }
    }

    private static void WriteString(BinaryWriter Writer, string Value)
    {
        byte[] Bytes = Encoding.UTF8.GetBytes(Value);
        Writer.Write(Bytes.Length);
        Writer.Write(Bytes);
    }

    // ---- Overrides (native -> managed): apply a self-describing value blob onto a live instance ----

    public static unsafe void ApplyValues(object Instance, IReadOnlyList<ScriptProperty> Properties, byte* Blob, int Length)
    {
        var Reader = new FBlobReader(new ReadOnlySpan<byte>(Blob, Length));
        int Count = Reader.ReadInt32();
        for (int Index = 0; Index < Count; Index++)
        {
            string Name = Reader.ReadString();
            ScriptProperty? Property = FindProperty(Properties, Name);
            if (Property == null)
            {
                SkipValue(ref Reader);
                continue;
            }

            if (ReadValue(ref Reader, Property.Type, out object? Value))
            {
                Property.Set(Instance, Value);
            }
        }
    }

    private static ScriptProperty? FindProperty(IReadOnlyList<ScriptProperty> Properties, string Name)
    {
        foreach (ScriptProperty Property in Properties)
        {
            if (Property.Name == Name)
            {
                return Property;
            }
        }
        return null;
    }

    /// <summary>
    /// Reads one self-describing value. If the encoded kind doesn't match the target shape (schema
    /// drift) the bytes are consumed and false is returned so the field keeps its default.
    /// </summary>
    private static bool ReadValue(ref FBlobReader Reader, ScriptType Type, out object? Value)
    {
        Value = null;

        // Asset references arrive as a path string (wire kind String); rebuild the typed ref via IAssetRef.
        if (Type.IsAssetRef)
        {
            var RefKind = (EScriptKind)Reader.ReadByte();
            if (RefKind != EScriptKind.String)
            {
                SkipBody(ref Reader, RefKind);
                return false;
            }
            string Path = Reader.ReadString();
            object? Box = Activator.CreateInstance(Type.Clr);
            if (Box is IAssetRef Reference)
            {
                Reference.SetFromPath(Path);
            }
            Value = Box;
            return Box != null;
        }

        var Kind = (EScriptKind)Reader.ReadByte();
        if (Kind != Type.Kind)
        {
            SkipBody(ref Reader, Kind);
            return false;
        }

        switch (Kind)
        {
            case EScriptKind.Bool:
            {
                Value = Reader.ReadByte() != 0;
                return true;
            }
            case EScriptKind.Int:
            {
                Value = CoerceInt(Reader.ReadInt64(), Type.Clr);
                return true;
            }
            case EScriptKind.Double:
            {
                // Box the EXACT field type: a `? (float) : (double)` ternary re-widens both branches to
                // double, so a float field would get a boxed double and FieldInfo.SetValue throws.
                double Number = Reader.ReadDouble();
                if (Type.Clr == typeof(float))
                {
                    Value = (float)Number;
                }
                else
                {
                    Value = Number;
                }
                return true;
            }
            case EScriptKind.String:
            {
                Value = Reader.ReadString();
                return true;
            }
            case EScriptKind.Vec2:
            {
                Value = new FVector2(Reader.ReadSingle(), Reader.ReadSingle());
                return true;
            }
            case EScriptKind.Vec3:
            {
                Value = new FVector3(Reader.ReadSingle(), Reader.ReadSingle(), Reader.ReadSingle());
                return true;
            }
            case EScriptKind.Vec4:
            {
                Value = new FVector4(Reader.ReadSingle(), Reader.ReadSingle(), Reader.ReadSingle(), Reader.ReadSingle());
                return true;
            }
            case EScriptKind.Array:
            {
                return ReadArray(ref Reader, Type, out Value);
            }
            case EScriptKind.NestedStruct:
            {
                return ReadNested(ref Reader, Type, out Value);
            }
            default:
            {
                return false;
            }
        }
    }

    private static bool ReadArray(ref FBlobReader Reader, ScriptType Type, out object? Value)
    {
        Value = null;
        int Count = Reader.ReadInt32();
        ScriptType Element = Type.Element ?? new ScriptType();
        Type ElementClr = Element.Clr;

        var Items = new List<object?>(Count);
        for (int Index = 0; Index < Count; Index++)
        {
            if (ReadValue(ref Reader, Element, out object? Item))
            {
                Items.Add(Item);
            }
        }

        if (Type.Clr.IsArray)
        {
            Array Result = Array.CreateInstance(ElementClr, Items.Count);
            for (int Index = 0; Index < Items.Count; Index++)
            {
                Result.SetValue(Items[Index], Index);
            }
            Value = Result;
            return true;
        }

        // List<T>
        if (Activator.CreateInstance(Type.Clr) is IList List)
        {
            foreach (object? Item in Items)
            {
                List.Add(Item);
            }
            Value = List;
            return true;
        }

        return false;
    }

    private static bool ReadNested(ref FBlobReader Reader, ScriptType Type, out object? Value)
    {
        object? Box = Activator.CreateInstance(Type.Clr);
        IReadOnlyList<ScriptProperty> Fields = Type.Fields ?? Array.Empty<ScriptProperty>();

        int Count = Reader.ReadInt32();
        for (int Index = 0; Index < Count; Index++)
        {
            string Name = Reader.ReadString();
            ScriptProperty? Field = FindProperty(Fields, Name);
            if (Field == null)
            {
                SkipValue(ref Reader);
                continue;
            }
            if (Box != null && ReadValue(ref Reader, Field.Type, out object? FieldValue))
            {
                Field.Set(Box, FieldValue);
            }
        }

        Value = Box;
        return Box != null;
    }

    private static object CoerceInt(long Value, Type Target)
    {
        if (Target.IsEnum)
        {
            return Enum.ToObject(Target, Value);
        }
        if (Target == typeof(long) || Target == typeof(ulong))
        {
            return Target == typeof(ulong) ? (object)(ulong)Value : Value;
        }
        return Convert.ChangeType(Value, Target);
    }

    private static void SkipValue(ref FBlobReader Reader)
    {
        var Kind = (EScriptKind)Reader.ReadByte();
        SkipBody(ref Reader, Kind);
    }

    private static void SkipBody(ref FBlobReader Reader, EScriptKind Kind)
    {
        switch (Kind)
        {
            case EScriptKind.Bool:
            {
                Reader.Skip(1);
                break;
            }
            case EScriptKind.Int:
            {
                Reader.Skip(8);
                break;
            }
            case EScriptKind.Double:
            {
                Reader.Skip(8);
                break;
            }
            case EScriptKind.String:
            {
                Reader.Skip(Reader.ReadInt32());
                break;
            }
            case EScriptKind.Vec2:
            {
                Reader.Skip(8);
                break;
            }
            case EScriptKind.Vec3:
            {
                Reader.Skip(12);
                break;
            }
            case EScriptKind.Vec4:
            {
                Reader.Skip(16);
                break;
            }
            case EScriptKind.Array:
            {
                int Count = Reader.ReadInt32();
                for (int Index = 0; Index < Count; Index++)
                {
                    SkipValue(ref Reader);
                }
                break;
            }
            case EScriptKind.NestedStruct:
            {
                int Count = Reader.ReadInt32();
                for (int Index = 0; Index < Count; Index++)
                {
                    Reader.Skip(Reader.ReadInt32()); // field name
                    SkipValue(ref Reader);
                }
                break;
            }
        }
    }
}

/// <summary>Little-endian cursor over a native value blob.</summary>
internal ref struct FBlobReader
{
    private ReadOnlySpan<byte> Span;
    private int Position;

    public FBlobReader(ReadOnlySpan<byte> Span)
    {
        this.Span = Span;
        Position = 0;
    }

    public byte ReadByte()
    {
        if (Position >= Span.Length)
        {
            return 0;
        }
        return Span[Position++];
    }

    public int ReadInt32()
    {
        if (Position + 4 > Span.Length)
        {
            Position = Span.Length;
            return 0;
        }
        int Value = BitConverter.ToInt32(Span.Slice(Position, 4));
        Position += 4;
        return Value;
    }

    public long ReadInt64()
    {
        if (Position + 8 > Span.Length)
        {
            Position = Span.Length;
            return 0;
        }
        long Value = BitConverter.ToInt64(Span.Slice(Position, 8));
        Position += 8;
        return Value;
    }

    public double ReadDouble()
    {
        if (Position + 8 > Span.Length)
        {
            Position = Span.Length;
            return 0.0;
        }
        double Value = BitConverter.ToDouble(Span.Slice(Position, 8));
        Position += 8;
        return Value;
    }

    public float ReadSingle()
    {
        if (Position + 4 > Span.Length)
        {
            Position = Span.Length;
            return 0.0f;
        }
        float Value = BitConverter.ToSingle(Span.Slice(Position, 4));
        Position += 4;
        return Value;
    }

    public string ReadString()
    {
        int Length = ReadInt32();
        if (Length <= 0 || Position + Length > Span.Length)
        {
            return string.Empty;
        }
        string Value = Encoding.UTF8.GetString(Span.Slice(Position, Length));
        Position += Length;
        return Value;
    }

    public void Skip(int Bytes)
    {
        if (Bytes < 0)
        {
            return;
        }
        Position = Math.Min(Span.Length, Position + Bytes);
    }
}
