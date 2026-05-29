#pragma once
#include "ReflectedProperty.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/CodeGeneration/CodeWriter.h"

namespace Lumina
{
    /** Mirror of FReflectedObjectProperty for TSoftObjectPtr<T>/FSoftObjectPath.
     *  Emits a SoftObjectProperty so the runtime serializer routes through
     *  FSoftObjectPath::operator<<, which writes (Path, GUID) and registers
     *  the GUID with the saver for cook-graph Soft edges. */
    class FReflectedSoftObjectProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return "SoftObject"; }
        const char* GetPropertyParamType() const override { return "FSoftObjectPropertyParams"; }
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
