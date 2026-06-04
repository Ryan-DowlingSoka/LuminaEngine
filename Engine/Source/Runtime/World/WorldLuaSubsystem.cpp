#include "WorldLuaSubsystem.h"

namespace Lumina
{
    FWorldLuaSubsystemRegistry& FWorldLuaSubsystemRegistry::Get()
    {
        static FWorldLuaSubsystemRegistry Instance;
        return Instance;
    }

    void FWorldLuaSubsystemRegistry::Register(FName Name, const char* LuauType, FWorldLuaSubsystemPush Push)
    {
        if (Push == nullptr)
        {
            return;
        }

        for (FEntry& Entry : Entries)
        {
            if (Entry.Name == Name)
            {
                Entry.LuauType = LuauType;
                Entry.Push     = Push;
                return;
            }
        }

        Entries.push_back(FEntry{ Name, LuauType, Push });
    }

    FWorldLuaSubsystemPush FWorldLuaSubsystemRegistry::Find(FName Name) const
    {
        for (const FEntry& Entry : Entries)
        {
            if (Entry.Name == Name)
            {
                return Entry.Push;
            }
        }
        return nullptr;
    }

    void RegisterWorldLuaSubsystem(FName Name, const char* LuauType, FWorldLuaSubsystemPush Push)
    {
        FWorldLuaSubsystemRegistry::Get().Register(Name, LuauType, Push);
    }
}
