#pragma once
#include "ReflectedProperty.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/CodeGeneration/CodeWriter.h"
#include "Reflector/CodeGeneration/ReflectionNames.h"

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
            const eastl::string Friendly = ClangUtils::MakeCodeFriendlyNamespace(TypeName);
            const eastl::string FnName = "Construct_CClass_" + Friendly;
            Reflection::Names::EmitGuardedCrossModuleDecl(Writer, API, "CClass", FnName);
        }
    };
}
