#include "ReflectedArrayProperty.h"

#include <cstdio>

#include "Reflector/CodeGeneration/CodeWriter.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina
{
    namespace
    {
        // The fat-tail of FArrayPropertyParams: function-pointer suffixes appended after
        // the common FPropertyParams fields. Order must match the runtime struct layout.
        constexpr const char* kArrayFatTailSuffixes[] =
        {
            "ArrayPushBack_WrapperImpl",
            "ArrayGetNum_WrapperImpl",
            "ArrayRemoveAt_WrapperImpl",
            "ArrayClear_WrapperImpl",
            "ArrayGetAt_WrapperImpl",
            "ArrayResize_WrapperImpl",
            "ArrayReserve_WrapperImpl",
            "ArraySwap_WrapperImpl",
        };
    }

    void FReflectedArrayProperty::AppendDefinition(Reflection::FCodeWriter& Writer) const
    {
        eastl::string CustomData;
        constexpr size_t kNumSuffixes = std::size(kArrayFatTailSuffixes);
        for (size_t i = 0; i < kNumSuffixes; ++i)
        {
            if (i > 0)
            {
                CustomData += ", ";
            }
            CustomData += Outer + "::" + Name + kArrayFatTailSuffixes[i];
        }

        const eastl::string PropertyFlagStr = PropertyFlagsToString(PropertyFlags);
        AppendPropertyDef(Writer, PropertyFlagStr.c_str(), "Lumina::EPropertyTypeFlags::Vector", CustomData);
    }

    bool FReflectedArrayProperty::HasAccessors()
    {
        return true;
    }

    bool FReflectedArrayProperty::DeclareAccessors(Reflection::FCodeWriter& Writer, const eastl::string& FileID)
    {
        FReflectedProperty::DeclareAccessors(Writer, FileID);

        Writer.Macrof("static void %sArrayGetter_WrapperImpl(const void* Object, void* OutValue);", Name.c_str());
        Writer.Macrof("static void %sArrayPushBack_WrapperImpl(void* Object, const void* InValue);", Name.c_str());
        Writer.Macrof("static size_t %sArrayGetNum_WrapperImpl(const void* Object);", Name.c_str());
        Writer.Macrof("static void %sArrayRemoveAt_WrapperImpl(void* Object, size_t Index);", Name.c_str());
        Writer.Macrof("static void %sArrayClear_WrapperImpl(void* Object);", Name.c_str());
        Writer.Macrof("static void* %sArrayGetAt_WrapperImpl(void* Object, size_t Index);", Name.c_str());
        Writer.Macrof("static void %sArrayResize_WrapperImpl(void* Object, size_t Size);", Name.c_str());
        Writer.Macrof("static void %sArrayReserve_WrapperImpl(void* Object, size_t Size);", Name.c_str());
        Writer.Macrof("static void %sArraySwap_WrapperImpl(void* Object, size_t LHS, size_t RHS);", Name.c_str());

        return true;
    }

    bool FReflectedArrayProperty::DefineAccessors(Reflection::FCodeWriter& Writer, Reflection::FReflectedType* ReflectedType)
    {
        FReflectedProperty::DefineAccessors(Writer, ReflectedType);

        const eastl::string& Q = ReflectedType->QualifiedName;
        const eastl::string& D = ReflectedType->DisplayName;
        const char* N = Name.c_str();

        // Getter (exposes the raw vector pointer for debug / inspection).
        Writer.Linef("void %s::%sArrayGetter_WrapperImpl(const void* Object, void* OutValue)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("const %s* Obj = (const %s*)Object;", D.c_str(), D.c_str());
        Writer.Linef("*(const %s**)OutValue = &Obj->%s;", RawTypeName.c_str(), N);
        Writer.EndBlock();
        Writer.Line();

        // PushBack: copy-emplace when given a value, default-construct when null.
        Writer.Linef("void %s::%sArrayPushBack_WrapperImpl(void* Object, const void* InValue)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("%s* Obj = (%s*)Object;", D.c_str(), D.c_str());
        Writer.Line("if (InValue)");
        Writer.BeginBlock();
        Writer.Linef("Obj->%s.push_back(*(const %s*)InValue);", N, ElementTypeName.c_str());
        Writer.EndBlock();
        Writer.Line("else");
        Writer.BeginBlock();
        Writer.Linef("Obj->%s.emplace_back();", N);
        Writer.EndBlock();
        Writer.EndBlock();
        Writer.Line();

        // Single-line wrappers: (ReturnType, function-name-suffix + signature, body printf).
        // Body gets Name printf'd in up to twice.
        struct FSimple
        {
            const char* Return;
            const char* Suffix;    // "ArrayGetNum_WrapperImpl(const void* Object)"
            bool        bConstObj;
            const char* BodyFmt;
        };
        const FSimple Simples[] =
        {
            { "size_t", "ArrayGetNum_WrapperImpl(const void* Object)",                  true,  "return Obj->%s.size();" },
            { "void",   "ArrayRemoveAt_WrapperImpl(void* Object, size_t Index)",        false, "Obj->%s.erase(Obj->%s.begin() + Index);" },
            { "void",   "ArrayClear_WrapperImpl(void* Object)",                         false, "Obj->%s.clear();" },
            { "void*",  "ArrayGetAt_WrapperImpl(void* Object, size_t Index)",           false, "return &Obj->%s[Index];" },
            { "void",   "ArrayResize_WrapperImpl(void* Object, size_t Size)",           false, "Obj->%s.resize(Size);" },
            { "void",   "ArrayReserve_WrapperImpl(void* Object, size_t Size)",          false, "Obj->%s.reserve(Size);" },
            { "void",   "ArraySwap_WrapperImpl(void* Object, size_t RHS, size_t LHS)",  false, "std::swap(Obj->%s[RHS], Obj->%s[LHS]);" },
        };

        for (const FSimple& S : Simples)
        {
            Writer.Linef("%s %s::%s%s", S.Return, Q.c_str(), N, S.Suffix);
            Writer.BeginBlock();
            Writer.Linef("%s%s* Obj = (%s%s*)Object;",
                S.bConstObj ? "const " : "", D.c_str(),
                S.bConstObj ? "const " : "", D.c_str());

            char Formatted[256];
            std::snprintf(Formatted, sizeof(Formatted), S.BodyFmt, N, N);
            Writer.Line(Formatted);

            Writer.EndBlock();
            Writer.Line();
        }

        return true;
    }

    bool FReflectedArrayProperty::GenerateLuaBinding(Reflection::FCodeWriter& Writer)
    {
        return true;
    }
}
