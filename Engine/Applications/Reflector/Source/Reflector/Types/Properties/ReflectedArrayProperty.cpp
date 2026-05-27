#include "ReflectedArrayProperty.h"

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
        const char* N = Name.c_str();
        const char* Raw = RawTypeName.c_str();      // The container type, e.g. TVector<T>.
        const char* Elem = ElementTypeName.c_str(); // The element type T.

        // Object is the container instance itself (&TVector<T>), not the owning struct.
        // The caller resolves the member offset via GetValuePtr, so arrays compose.

        // Getter (exposes the raw vector pointer for debug / inspection).
        Writer.Linef("void %s::%sArrayGetter_WrapperImpl(const void* Object, void* OutValue)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("*(const %s**)OutValue = (const %s*)Object;", Raw, Raw);
        Writer.EndBlock();
        Writer.Line();

        // PushBack: copy-emplace when given a value, default-construct when null.
        Writer.Linef("void %s::%sArrayPushBack_WrapperImpl(void* Object, const void* InValue)", Q.c_str(), N);
        Writer.BeginBlock();
        Writer.Linef("%s* Vec = (%s*)Object;", Raw, Raw);
        Writer.Line("if (InValue)");
        Writer.BeginBlock();
        Writer.Linef("Vec->push_back(*(const %s*)InValue);", Elem);
        Writer.EndBlock();
        Writer.Line("else");
        Writer.BeginBlock();
        Writer.Line("Vec->emplace_back();");
        Writer.EndBlock();
        Writer.EndBlock();
        Writer.Line();

        // Single-line wrappers: (ReturnType, function-name-suffix + signature, const-Object, body).
        struct FSimple
        {
            const char* Return;
            const char* Suffix;
            bool        bConstObj;
            const char* Body;
        };
        const FSimple Simples[] =
        {
            { "size_t", "ArrayGetNum_WrapperImpl(const void* Object)",                  true,  "return Vec->size();" },
            { "void",   "ArrayRemoveAt_WrapperImpl(void* Object, size_t Index)",        false, "Vec->erase(Vec->begin() + Index);" },
            { "void",   "ArrayClear_WrapperImpl(void* Object)",                         false, "Vec->clear();" },
            { "void*",  "ArrayGetAt_WrapperImpl(void* Object, size_t Index)",           false, "return &(*Vec)[Index];" },
            { "void",   "ArrayResize_WrapperImpl(void* Object, size_t Size)",           false, "Vec->resize(Size);" },
            { "void",   "ArrayReserve_WrapperImpl(void* Object, size_t Size)",          false, "Vec->reserve(Size);" },
            { "void",   "ArraySwap_WrapperImpl(void* Object, size_t LHS, size_t RHS)",  false, "std::swap((*Vec)[LHS], (*Vec)[RHS]);" },
        };

        for (const FSimple& S : Simples)
        {
            Writer.Linef("%s %s::%s%s", S.Return, Q.c_str(), N, S.Suffix);
            Writer.BeginBlock();
            Writer.Linef("%s%s* Vec = (%s%s*)Object;",
                S.bConstObj ? "const " : "", Raw,
                S.bConstObj ? "const " : "", Raw);
            Writer.Line(S.Body);
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
