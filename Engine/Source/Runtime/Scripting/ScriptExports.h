#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"
#include "Core/Math/Math.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class FArchive;
}

// The neutral per-instance script-property schema + value model. Originally authored for Lua exports; now
// the single source of truth shared by the C# [Property] path (DotNetHost / CSharpScriptComponent /
// ScriptPropertyDrawer). Pure data - no VM coupling.
namespace Lumina::Scripting
{
    // Discriminated kinds for a per-instance script property.
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

    // One editor-display metadata pair (Category/Tooltip/Units/Min/Max/AssetType/...), set on a field.
    struct FScriptExportMetaArg
    {
        FName   Key;
        FString Value;
    };

    struct RUNTIME_API FScriptExportMeta
    {
        TVector<FScriptExportMetaArg> Entries;

        const FString* Find(const FName& Key) const;
        bool Has(const FName& Key) const { return Find(Key) != nullptr; }
        void Set(const FName& Key, const FString& Value);
        bool GetNumber(const FName& Key, double& OutValue) const;
    };

    struct FScriptExportField
    {
        FName                         Name;
        TSharedPtr<FScriptExportType> Type;
        FScriptExportMeta             Meta;   ///< Editor display data, rebuilt per load.
    };

    struct FScriptExportType
    {
        EScriptExportKind             Kind = EScriptExportKind::Nil;
        FName                         UserdataTypeName;          ///< When Kind == UnknownUserdata.
        TSharedPtr<FScriptExportType> ElementType;               ///< When Kind == Array.
        TVector<FScriptExportField>   Fields;                    ///< When Kind == NestedStruct.
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
        FVector4                    AsVec    {0.0f};     ///< Covers vec2/3/4.
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

    // Drops type-mismatched fields, fills missing ones with defaults.
    RUNTIME_API void ReconcileOverrides(const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Defaults, TVector<FScriptPropertyEntry>& InOutOverrides);
}
