#pragma once

#include "ReflectedProperty.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/CodeGeneration/CodeWriter.h"


namespace Lumina
{
    class FReflectedEnumProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return "Enum"; }
        const char* GetPropertyParamType() const override { return "FEnumPropertyParams"; }
        eastl::string_view GetLuaType() override { return "number"; }

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override
        {
            const eastl::string PropertyFlagStr = PropertyFlagsToString(PropertyFlags);
            const eastl::string CustomData = "Construct_CEnum_" + ClangUtils::MakeCodeFriendlyNamespace(TypeName);
            AppendPropertyDef(Writer, PropertyFlagStr.c_str(), "Lumina::EPropertyTypeFlags::Enum", CustomData);
        }

        bool CanDeclareCrossModuleReferences() const override { return true; }
        void DeclareCrossModuleReference(const eastl::string& API, Reflection::FCodeWriter& Writer) override
        {
            Writer.Linef("%s Lumina::CEnum* Construct_CEnum_%s();",
                API.c_str(),
                ClangUtils::MakeCodeFriendlyNamespace(TypeName).c_str());
        }
    };
}
