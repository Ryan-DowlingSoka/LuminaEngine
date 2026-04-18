#pragma once
#include "ReflectedProperty.h"

namespace Lumina
{
    class FReflectedArrayProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return nullptr; }
        const char* GetPropertyParamType() const override { return "FArrayPropertyParams"; }
        eastl::string_view GetLuaType() override { return eastl::string_view{}; }

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override;

        bool HasAccessors() override;
        bool DeclareAccessors(Reflection::FCodeWriter& Writer, const eastl::string& FileID) override;
        bool DefineAccessors(Reflection::FCodeWriter& Writer, Reflection::FReflectedType* ReflectedType) override;
        bool GenerateLuaBinding(Reflection::FCodeWriter& Writer) override;

        eastl::string ElementTypeName;
    };
}
