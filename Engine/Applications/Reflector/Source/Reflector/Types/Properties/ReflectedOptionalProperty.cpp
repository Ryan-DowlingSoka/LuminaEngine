#include "ReflectedOptionalProperty.h"

#include "Reflector/CodeGeneration/CodeWriter.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina
{
    namespace
    {
        // Order must match FOptionalPropertyParams: HasValue, GetValue, SetValue, Reset.
        constexpr const char* kOptionalFatTailSuffixes[] =
        {
            "OptionalHasValue_WrapperImpl",
            "OptionalGetValue_WrapperImpl",
            "OptionalSetValue_WrapperImpl",
            "OptionalReset_WrapperImpl",
        };
    }

    void FReflectedOptionalProperty::AppendDefinition(Reflection::FCodeWriter& Writer) const
    {
        eastl::string CustomData;
        constexpr size_t kNumSuffixes = std::size(kOptionalFatTailSuffixes);
        for (size_t i = 0; i < kNumSuffixes; ++i)
        {
            if (i > 0)
            {
                CustomData += ", ";
            }
            CustomData += Outer + "::" + Name + kOptionalFatTailSuffixes[i];
        }

        const eastl::string PropertyFlagStr = PropertyFlagsToString(PropertyFlags);
        AppendPropertyDef(Writer, PropertyFlagStr.c_str(), "Lumina::EPropertyTypeFlags::Optional", CustomData);
    }

    bool FReflectedOptionalProperty::HasAccessors()
    {
        return true;
    }

    bool FReflectedOptionalProperty::DeclareAccessors(Reflection::FCodeWriter& Writer, const eastl::string& FileID)
    {
        FReflectedProperty::DeclareAccessors(Writer, FileID);

        Writer.Macrof("static bool  %sOptionalHasValue_WrapperImpl(const void* Object);", Name.c_str());
        Writer.Macrof("static void* %sOptionalGetValue_WrapperImpl(void* Object);", Name.c_str());
        Writer.Macrof("static void  %sOptionalSetValue_WrapperImpl(void* Object, const void* InValue);", Name.c_str());
        Writer.Macrof("static void  %sOptionalReset_WrapperImpl(void* Object);", Name.c_str());

        return true;
    }

    bool FReflectedOptionalProperty::DefineAccessors(Reflection::FCodeWriter& Writer, Reflection::FReflectedType* ReflectedType)
    {
        FReflectedProperty::DefineAccessors(Writer, ReflectedType);

        const eastl::string& Q = ReflectedType->QualifiedName;
        const eastl::string& D = ReflectedType->DisplayName;
        const char* N = Name.c_str();

        // HasValue
        Writer.Linef("bool %s::%sOptionalHasValue_WrapperImpl(const void* Object)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("const %s* Obj = (const %s*)Object;", D.c_str(), D.c_str());
        Writer.Linef("return Obj->%s.has_value();", N);
        Writer.EndBlock();
        Writer.Line();

        // GetValue (raw pointer to held T; only valid when HasValue is true).
        Writer.Linef("void* %s::%sOptionalGetValue_WrapperImpl(void* Object)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("%s* Obj = (%s*)Object;", D.c_str(), D.c_str());
        Writer.Linef("return Obj->%s.has_value() ? &Obj->%s.value() : nullptr;", N, N);
        Writer.EndBlock();
        Writer.Line();

        // SetValue: copy-emplace when given a value, default-construct when null.
        Writer.Linef("void %s::%sOptionalSetValue_WrapperImpl(void* Object, const void* InValue)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("%s* Obj = (%s*)Object;", D.c_str(), D.c_str());
        Writer.Line("if (InValue)");
        Writer.BeginBlock();
        Writer.Linef("Obj->%s = *(const %s*)InValue;", N, ElementTypeName.c_str());
        Writer.EndBlock();
        Writer.Line("else");
        Writer.BeginBlock();
        Writer.Linef("Obj->%s.emplace();", N);
        Writer.EndBlock();
        Writer.EndBlock();
        Writer.Line();

        // Reset
        Writer.Linef("void %s::%sOptionalReset_WrapperImpl(void* Object)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("%s* Obj = (%s*)Object;", D.c_str(), D.c_str());
        Writer.Linef("Obj->%s.reset();", N);
        Writer.EndBlock();
        Writer.Line();

        return true;
    }

    bool FReflectedOptionalProperty::GenerateLuaBinding(Reflection::FCodeWriter& Writer)
    {
        return true;
    }
}
