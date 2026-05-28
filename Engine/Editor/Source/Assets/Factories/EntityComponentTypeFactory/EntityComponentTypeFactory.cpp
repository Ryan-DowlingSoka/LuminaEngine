#include "pch.h"
#include "EntityComponentTypeFactory.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Scripting/Lua/Scripting.h"
#include "World/Entity/RuntimeComponent.h"

namespace Lumina
{
    CObject* CEntityComponentTypeFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        CEntityComponentType* Type = NewObject<CEntityComponentType>(Package, Name);

        if (lua_State* VM = Lua::FScriptingContext::Get().GetVM())
        {
            RegisterRuntimeComponentTypeGlobal(VM, Type);
        }

        return Type;
    }
}
