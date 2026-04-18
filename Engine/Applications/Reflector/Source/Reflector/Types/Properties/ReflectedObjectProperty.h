#pragma once
#include "ReflectedProperty.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/CodeGeneration/CodeWriter.h"

namespace Lumina
{
    class FReflectedObjectProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return "Object"; }
        const char* GetPropertyParamType() const override { return "FObjectPropertyParams"; }
        eastl::string_view GetLuaType() override;

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override;
        bool GenerateLuaBinding(Reflection::FCodeWriter& Writer) override { return true; }

        void DeclareCrossModuleReference(const eastl::string& API, Reflection::FCodeWriter& Writer) override
        {
            Writer.Linef("%s Lumina::CClass* Construct_CClass_%s();",
                API.c_str(),
                ClangUtils::MakeCodeFriendlyNamespace(TypeName).c_str());
        }
    };
}
