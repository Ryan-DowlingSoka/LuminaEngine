#pragma once
#include "ReflectedProperty.h"
#include "Reflector/CodeGeneration/CodeWriter.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina
{
    class FReflectedStringProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return "FString"; }
        const char* GetPropertyParamType() const override { return "FStringPropertyParams"; }
        eastl::string_view GetLuaType() override { return "string"; }

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override
        {
            const eastl::string PropertyFlagStr = PropertyFlagsToString(PropertyFlags);
            AppendPropertyDef(Writer, PropertyFlagStr.c_str(), "Lumina::EPropertyTypeFlags::String");
        }

        bool GenerateLuaBinding(Reflection::FCodeWriter& Writer) override;
    };

    class FReflectedNameProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return "FName"; }
        const char* GetPropertyParamType() const override { return "FNamePropertyParams"; }
        eastl::string_view GetLuaType() override { return "string"; }

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override
        {
            const eastl::string PropertyFlagStr = PropertyFlagsToString(PropertyFlags);
            AppendPropertyDef(Writer, PropertyFlagStr.c_str(), "Lumina::EPropertyTypeFlags::Name");
        }
    };
}
