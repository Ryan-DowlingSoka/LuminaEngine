#include "ReflectedProperty.h"

#include "Reflector/CodeGeneration/CodeWriter.h"
#include "Reflector/Types/PropertyFlags.h"
#include "Reflector/Types/ReflectedType.h"
#include "Reflector/Utils/MetadataUtils.h"

namespace Lumina
{
    void FReflectedProperty::AppendPropertyDef(Reflection::FCodeWriter& Writer, const char* PropertyFlagsStr, const char* TypeFlags, const eastl::string& CustomData) const
    {
        const eastl::string GetterFunctionName = GetterFunc.empty() ? "nullptr" : (Outer + "::" + GetterFunc + "_WrapperImpl");
        const eastl::string SetterFunctionName = SetterFunc.empty() ? "nullptr" : (Outer + "::" + SetterFunc + "_WrapperImpl");
        const eastl::string Offset = bInner ? eastl::string("0") : ("offsetof(" + Outer + ", " + Name + ")");

        Writer.Appendf("{ \"%s\", %s, %s, %s, %s, %s",
            Name.c_str(),
            PropertyFlagsStr,
            TypeFlags,
            SetterFunctionName.c_str(),
            GetterFunctionName.c_str(),
            Offset.c_str());

        if (!CustomData.empty())
        {
            Writer.Appendf(", %s", CustomData.c_str());
        }

        if (!Metadata.empty())
        {
            Writer.Appendf(", METADATA_PARAMS(std::size(%s_Metadata), %s_Metadata)", Name.c_str(), Name.c_str());
        }

        Writer.Line(" };");
    }

    void FReflectedProperty::GenerateMetadata(const eastl::string& InMetadata)
    {
        if (InMetadata.empty())
        {
            return;
        }

        FMetadataParser Parser(InMetadata);
        Metadata = eastl::move(Parser.Metadata);

        for (const FMetadataPair& MetadataPair : Metadata)
        {
            if (MetadataPair.Key == "ReadOnly")
            {
                PropertyFlags |= EPropertyFlags::ReadOnly;
            }
            else if (MetadataPair.Key == "NoSerialize")
            {
                PropertyFlags |= EPropertyFlags::NoSerialize;
            }
            else if (MetadataPair.Key == "Editable")
            {
                PropertyFlags |= EPropertyFlags::Editable;
            }
            else if (MetadataPair.Key == "Script")
            {
                PropertyFlags |= EPropertyFlags::Script;
            }
            else if (MetadataPair.Key == "EditorOnly")
            {
                PropertyFlags |= EPropertyFlags::EditorOnly;
            }
            else if (MetadataPair.Key == "Getter")
            {
                GetterFunc = MetadataPair.Value.empty() ? ("Get" + Name) : MetadataPair.Value;
            }
            else if (MetadataPair.Key == "Setter")
            {
                SetterFunc = MetadataPair.Value.empty() ? ("Set" + Name) : MetadataPair.Value;
            }
        }
    }

    bool FReflectedProperty::GenerateLuaBinding(Reflection::FCodeWriter& Writer)
    {
        Writer.Appendf("\t\t\"%s\", &%s::%s", GetDisplayName().c_str(), Outer.c_str(), GetDisplayName().c_str());
        return true;
    }

    bool FReflectedProperty::HasAccessors()
    {
        return !GetterFunc.empty() || !SetterFunc.empty();
    }

    bool FReflectedProperty::DeclareAccessors(Reflection::FCodeWriter& Writer, const eastl::string& FileID)
    {
        if (!GetterFunc.empty())
        {
            Writer.Macrof("static void %s_WrapperImpl(const void* Object, void* OutValue);", GetterFunc.c_str());
        }

        if (!SetterFunc.empty())
        {
            Writer.Macrof("static void %s_WrapperImpl(void* Object, const void* InValue);", SetterFunc.c_str());
        }

        return HasAccessors();
    }

    bool FReflectedProperty::DefineAccessors(Reflection::FCodeWriter& Writer, Reflection::FReflectedType* ReflectedType)
    {
        if (!GetterFunc.empty())
        {
            Writer.Linef("void %s::%s_WrapperImpl(const void* Object, void* OutValue)", ReflectedType->QualifiedName.c_str(), GetterFunc.c_str());
            Writer.BeginBlock();
            Writer.Linef("const %s* Obj = (const %s*)Object;", ReflectedType->DisplayName.c_str(), ReflectedType->DisplayName.c_str());
            Writer.Linef("%s& Result = *(%s*)OutValue;", RawTypeName.c_str(), RawTypeName.c_str());
            Writer.Linef("Result = (%s)Obj->%s();", RawTypeName.c_str(), GetterFunc.c_str());
            Writer.EndBlock();
            Writer.Line();
        }

        if (!SetterFunc.empty())
        {
            Writer.Linef("void %s::%s_WrapperImpl(void* Object, const void* InValue)", ReflectedType->QualifiedName.c_str(), SetterFunc.c_str());
            Writer.BeginBlock();
            Writer.Linef("%s* Obj = (%s*)Object;", ReflectedType->QualifiedName.c_str(), ReflectedType->QualifiedName.c_str());
            Writer.Linef("const %s& Value = *(const %s*)InValue;", RawTypeName.c_str(), RawTypeName.c_str());
            Writer.Linef("Obj->%s(Value);", SetterFunc.c_str());
            Writer.EndBlock();
            Writer.Line();
        }

        return true;
    }
}
