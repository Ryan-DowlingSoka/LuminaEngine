#include "pch.h"
#include "EntityComponentType.h"
#include "GUID/GUID.h"
#include "Scripting/Lua/Scripting.h"
#include "World/Entity/RuntimeComponent.h"

namespace Lumina
{
    void CEntityComponentType::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
        SchemaBag.Serialize(Ar);
        Ar << SchemaRevision;
    }

    void CEntityComponentType::PostLoad()
    {
        CObject::PostLoad();
        if (lua_State* VM = Lua::FScriptingContext::Get().GetVM())
        {
            RegisterRuntimeComponentTypeGlobal(VM, this);
        }
    }

    uint32 CEntityComponentType::GetStorageId() const
    {
        return static_cast<uint32>(GetGUID().Hash());
    }
}
