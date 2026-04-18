#pragma once

#include <EASTL/string.h>
#include "EASTL/vector.h"
#include "Reflector/Types/PropertyFlags.h"
#include "Reflector/Types/StructReflectItem.h"
#include "Reflector/Utils/MetadataUtils.h"


namespace Lumina::Reflection
{
    class FReflectedType;
    class FCodeWriter;
}

namespace Lumina
{
    /**
     * Base for every reflected property kind (numeric, string, struct, object, array, etc.).
     *
     * Subclass contract — override these to add a new property type:
     *   - GetPropertyParamType()  : the FXxxPropertyParams struct name emitted into the .cpp
     *   - GetTypeName()           : friendly display name (used for logs / Lua fallback)
     *   - GetLuaType()            : Luau type string (returned when emitting the .d.luau)
     *   - AppendDefinition(...)   : writes the FXxxPropertyParams initializer
     *
     * Optional overrides for property kinds that need accessor wrappers or cross-module
     * construct_* forward declarations:
     *   - HasAccessors / DeclareAccessors / DefineAccessors
     *   - CanDeclareCrossModuleReferences / DeclareCrossModuleReference
     *
     * The base class handles Getter/Setter wrappers driven by metadata, so most simple
     * property types only need to implement the four required hooks above.
     */
    class FReflectedProperty : public IStructReflectable
    {
    public:

        virtual ~FReflectedProperty() = default;

        virtual const char* GetPropertyParamType() const { return "FPropertyParams"; }
        virtual const char* GetTypeName() = 0;
        virtual eastl::string_view GetLuaType() = 0;

        virtual void AppendDefinition(Reflection::FCodeWriter& Writer) const = 0;

        virtual bool CanDeclareCrossModuleReferences() const { return false; }
        virtual void DeclareCrossModuleReference(const eastl::string& API, Reflection::FCodeWriter& Writer) { }

        virtual bool HasAccessors();
        virtual bool DeclareAccessors(Reflection::FCodeWriter& Writer, const eastl::string& FileID);
        virtual bool DefineAccessors(Reflection::FCodeWriter& Writer, Reflection::FReflectedType* ReflectedType);

        bool GenerateLuaBinding(Reflection::FCodeWriter& Writer) override;
        void GenerateMetadata(const eastl::string& InMetadata) override;

        eastl::string GetDisplayName() const { return Name; }

        // Emits the trailing `{ "Name", Flags, TypeFlags, Setter, Getter, Offset[, CustomData][, METADATA_PARAMS] };`
        // shared by every property kind.
        void AppendPropertyDef(Reflection::FCodeWriter& Writer, const char* PropertyFlagsStr, const char* TypeFlags, const eastl::string& CustomData = "") const;

        eastl::vector<FMetadataPair>    Metadata;
        EPropertyFlags                  PropertyFlags;
        eastl::string                   RawTypeName;
        eastl::string                   TypeName;
        eastl::string                   Namespace;
        eastl::string                   Name;
        eastl::string                   Outer;
        eastl::string                   GetterFunc;
        eastl::string                   SetterFunc;

        bool                            bInner = false;
    };
}
