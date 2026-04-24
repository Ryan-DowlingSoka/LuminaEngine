#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"
#include "glm/glm.hpp"
#include "Platform/GenericPlatform.h"

struct lua_State;

namespace Lumina
{
    class FArchive;
}

namespace Lumina::Lua
{
    /**
     * Discriminated kinds for per-instance script properties exported from a Luau script.
     * Scalars cover the common cases; UnknownUserdata records the declared type name for
     * reflected C++ structs that v1 does not yet edit per-instance.
     */
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
        NestedStruct,      ///< Table with named fields declared via `type T = { a: T, ... }`.
        UnknownUserdata,   ///< E.g. `STransformComponent`; editing deferred.
    };

    struct FScriptExportType;

    struct FScriptExportField
    {
        FName                       Name;
        TSharedPtr<FScriptExportType> Type;
    };

    /**
     * Schema inferred by walking the live `Exports` table produced by the script.
     * Nested forms (arrays, nested structs) recurse via the inner pointers.
     */
    struct FScriptExportType
    {
        EScriptExportKind           Kind = EScriptExportKind::Nil;
        FName                       UserdataTypeName;          ///< Populated when Kind == UnknownUserdata.
        TSharedPtr<FScriptExportType> ElementType;             ///< Populated when Kind == Array.
        TVector<FScriptExportField> Fields;                    ///< Populated when Kind == NestedStruct.
    };

    struct FScriptExportSchema
    {
        TVector<FScriptExportField> Fields;

        bool IsValid() const { return !Fields.empty(); }
    };

    struct FScriptPropertyEntry;

    /**
     * Runtime per-instance value carrying a discriminator plus just the storage the
     * kind requires. Self-serializing; safe against schema drift because reads are
     * driven by the stored Kind, and the apply step reconciles mismatches by type.
     */
    struct RUNTIME_API FScriptPropertyValue
    {
        EScriptExportKind           Kind = EScriptExportKind::Nil;

        bool                        AsBool   = false;
        int64                       AsInt    = 0;
        double                      AsDouble = 0.0;
        FString                     AsString;
        glm::vec4                   AsVec    {0.0f};     ///< Covers vec2/3/4.
        FName                       UserdataTypeName;

        TVector<FScriptPropertyValue> Items;             ///< Used when Kind == Array.
        TVector<FScriptPropertyEntry> StructFields;      ///< Used when Kind == NestedStruct.

        bool Serialize(FArchive& Ar);

        /** Constructs a default-valued instance matching Type. */
        static FScriptPropertyValue FromType(const FScriptExportType& Type);
    };

    struct RUNTIME_API FScriptPropertyEntry
    {
        FName                       Name;
        FScriptPropertyValue        Value;
    };

    /**
     * Walks the live `Exports` table once to infer the schema AND read the defaults.
     *
     * Duck-typing rules: booleans=>Bool, integral=>Int, fractional=>Double, strings=>String,
     * Luau vectors=>Vec3, registered userdata=>UnknownUserdata (type from metatable `__typename`
     * stamped by TClass), integer-keyed tables=>Array, string-keyed tables=>NestedStruct.
     *
     * The user must provide a non-nil default for each field; nil has no type info.
     */
    RUNTIME_API bool BuildSchemaFromExportsTable(
        lua_State* State,
        int ExportsTableIndex,
        FScriptExportSchema& OutSchema,
        TVector<FScriptPropertyEntry>& OutDefaults);

    /**
     * Apply the override entries into the Exports table by mutating fields in place.
     * The script holds a reference to the same table, so user code observes the change.
     */
    RUNTIME_API void ApplyOverridesToExportsTable(lua_State* State, int ExportsTableIndex, const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Overrides);

    /**
     * Bring an override list into sync with the current schema: drop fields whose type no
     * longer matches, insert missing fields filled with defaults read from the runtime table.
     */
    RUNTIME_API void ReconcileOverrides(const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Defaults, TVector<FScriptPropertyEntry>& InOutOverrides);
}
