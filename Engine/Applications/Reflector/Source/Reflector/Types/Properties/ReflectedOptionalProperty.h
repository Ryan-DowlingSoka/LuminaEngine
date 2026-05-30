#pragma once
#include "ReflectedProperty.h"

namespace Lumina
{
    // Reflector model for TOptional<T>; emits HasValue/GetValue/SetValue/Reset wrappers.
    // Inner T is an adjacent property entry the runtime stitches in via FProperty::AddProperty.
    class FReflectedOptionalProperty : public FReflectedProperty
    {
    public:

        const char* GetTypeName() override { return nullptr; }
        const char* GetPropertyParamType() const override { return "FOptionalPropertyParams"; }
        eastl::string_view GetLuaType() override { return eastl::string_view{}; }

        void AppendDefinition(Reflection::FCodeWriter& Writer) const override;

        bool HasAccessors() override;
        bool DeclareAccessors(Reflection::FCodeWriter& Writer, const eastl::string& FileID) override;
        bool DefineAccessors(Reflection::FCodeWriter& Writer, Reflection::FReflectedType* ReflectedType) override;
        bool GenerateLuaBinding(Reflection::FCodeWriter& Writer) override;

        eastl::string ElementTypeName;
    };
}
