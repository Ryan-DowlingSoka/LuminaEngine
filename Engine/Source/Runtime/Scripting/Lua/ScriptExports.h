#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"
#include "Core/Math/Math.h"
#include "Platform/GenericPlatform.h"
#include "ScriptAnnotations.h"

struct lua_State;

namespace Lumina
{
    class FArchive;
}

namespace Lumina::Lua
{
    // Discriminated kinds for per-instance script properties exported from a Luau script.
    enum class EScriptExportKind : uint8
    {
        Nil = 0,
        Bool,
        Int,
        Double,
        String,
        Vec2,
        Vec3,
        Vec4,
        Array,             ///< Homogeneous; ElementType holds the schema.
        NestedStruct,
        UnknownUserdata,   ///< Reflected C++ userdata; editing deferred.
    };

    struct FScriptExportType;

    // PROPERTY-style metadata harvested from a field's --@export(...) annotation
    // (ClampMin/ClampMax/Delta/Units/Category/Tooltip/Color/NoDrag/ReadOnly). A thin
    // key/value lookup mirroring FProperty's HasMetadata/GetMetadata, so the editor
    // drives the same clamp/color/units widgets for script exports.
    struct RUNTIME_API FScriptExportMeta
    {
        TVector<FScriptAnnotationArg> Entries;

        const FString* Find(const FName& Key) const;
        bool Has(const FName& Key) const { return Find(Key) != nullptr; }
        void Set(const FName& Key, const FString& Value);
        bool GetNumber(const FName& Key, double& OutValue) const;
    };

    struct FScriptExportField
    {
        FName                         Name;
        TSharedPtr<FScriptExportType> Type;
        FScriptExportMeta             Meta;   ///< From --@export(...) on this field; editor display data, rebuilt per load.
    };

    struct FScriptExportType
    {
        EScriptExportKind           Kind = EScriptExportKind::Nil;
        FName                       UserdataTypeName;          ///< When Kind == UnknownUserdata.
        TSharedPtr<FScriptExportType> ElementType;             ///< When Kind == Array.
        TVector<FScriptExportField> Fields;                    ///< When Kind == NestedStruct.
    };

    struct FScriptExportSchema
    {
        TVector<FScriptExportField> Fields;

        bool IsValid() const { return !Fields.empty(); }
    };

    struct FScriptPropertyEntry;

    // Tagged-union style per-instance value; self-serializing, schema-drift safe via reconcile.
    struct RUNTIME_API FScriptPropertyValue
    {
        EScriptExportKind           Kind = EScriptExportKind::Nil;

        bool                        AsBool   = false;
        int64                       AsInt    = 0;
        double                      AsDouble = 0.0;
        FString                     AsString;
        FVector4                   AsVec    {0.0f};     ///< Covers vec2/3/4.
        FName                       UserdataTypeName;

        TVector<FScriptPropertyValue> Items;             ///< When Kind == Array.
        TVector<FScriptPropertyEntry> StructFields;      ///< When Kind == NestedStruct.

        bool Serialize(FArchive& Ar);

        static FScriptPropertyValue FromType(const FScriptExportType& Type);
    };

    struct RUNTIME_API FScriptPropertyEntry
    {
        FName                       Name;
        FScriptPropertyValue        Value;
    };

    // Schema + defaults for every --@export-annotated top-level member (`Script.<Name> = ...`),
    // in source order. Each member's value is read (raw) from the live script table to infer its
    // type and default; its --@export(...) args become the field's Meta. Members with no assigned
    // value are skipped. ScriptTableIndex is the live module table (the returned `Script`).
    RUNTIME_API bool BuildSchemaFromAnnotatedExports(
        lua_State* State,
        int ScriptTableIndex,
        const TVector<FScriptMemberAnnotation>& Annotations,
        FScriptExportSchema& OutSchema,
        TVector<FScriptPropertyEntry>& OutDefaults);

    // Writes each override straight onto the live script table as a top-level field (Script.<Name>).
    RUNTIME_API void ApplyOverridesToScriptTable(lua_State* State, int ScriptTableIndex, const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Overrides);

    // Drops type-mismatched fields, fills missing ones with defaults.
    RUNTIME_API void ReconcileOverrides(const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Defaults, TVector<FScriptPropertyEntry>& InOutOverrides);
}
