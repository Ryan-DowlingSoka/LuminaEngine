#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"
#include "Core/Math/Math.h"
#include "Platform/GenericPlatform.h"

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

    struct FScriptExportField
    {
        FName                       Name;
        TSharedPtr<FScriptExportType> Type;
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

    // Duck-typed schema + defaults from one walk of the live Exports table. Nil fields have no type info.
    RUNTIME_API bool BuildSchemaFromExportsTable(
        lua_State* State,
        int ExportsTableIndex,
        FScriptExportSchema& OutSchema,
        TVector<FScriptPropertyEntry>& OutDefaults);

    RUNTIME_API void ApplyOverridesToExportsTable(lua_State* State, int ExportsTableIndex, const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Overrides);

    // Drops type-mismatched fields, fills missing ones with defaults.
    RUNTIME_API void ReconcileOverrides(const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Defaults, TVector<FScriptPropertyEntry>& InOutOverrides);
}
