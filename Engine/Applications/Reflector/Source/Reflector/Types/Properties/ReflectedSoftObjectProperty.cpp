#include "ReflectedSoftObjectProperty.h"

#include "Reflector/CodeGeneration/CodeWriter.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina
{
    void FReflectedSoftObjectProperty::AppendDefinition(Reflection::FCodeWriter& Writer) const
    {
        const eastl::string PropertyFlagStr = PropertyFlagsToString(PropertyFlags);
        const eastl::string CustomData = "Construct_CClass_" + ClangUtils::MakeCodeFriendlyNamespace(TypeName);
        AppendPropertyDef(Writer, PropertyFlagStr.c_str(), "Lumina::EPropertyTypeFlags::SoftObject", CustomData);
    }

    eastl::string_view FReflectedSoftObjectProperty::GetLuaType()
    {
        const size_t Pos = TypeName.find_last_of(':');
        if (Pos != eastl::string::npos)
        {
            return eastl::string_view(TypeName).substr(Pos + 1);
        }
        return TypeName;
    }
}
