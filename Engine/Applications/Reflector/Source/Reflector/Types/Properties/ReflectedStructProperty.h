#pragma once
#include "ReflectedProperty.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/CodeGeneration/CodeWriter.h"

namespace Lumina
{
    class FReflectedStructProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return "Struct"; }
        const char* GetPropertyParamType() const override { return "FStructPropertyParams"; }
        eastl::string_view GetLuaType() override;

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override;

        bool CanDeclareCrossModuleReferences() const override { return true; }
        void DeclareCrossModuleReference(const eastl::string& API, Reflection::FCodeWriter& Writer) override
        {
            Writer.Linef("%s Lumina::CStruct* Construct_CStruct_%s();",
                API.c_str(),
                ClangUtils::MakeCodeFriendlyNamespace(TypeName).c_str());
        }
    };
}
