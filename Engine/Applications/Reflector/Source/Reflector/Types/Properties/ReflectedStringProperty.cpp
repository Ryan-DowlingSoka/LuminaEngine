#include "ReflectedStringProperty.h"

#include "Reflector/CodeGeneration/CodeWriter.h"


namespace Lumina
{
    bool FReflectedStringProperty::GenerateLuaBinding(Reflection::FCodeWriter& Writer)
    {
        Writer.Appendf("\t\t\"%s\", sol::property([](%s& Self) { return \"\"; })",
            GetDisplayName().c_str(), Outer.c_str());
        return true;
    }
}
