#include "pch.h"
#include "Traits.h"
#include "Core/Assertions/Assert.h"
#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "lua.h"

namespace Lumina::Lua
{
    uint16 FTypeIndex::GetOrCreate(const char* UniqueTypeName)
    {
        static FMutex Mutex;
        static THashMap<FString, uint16> Tags;
        static uint16 GNextTag = 1;

        FScopeLock Lock(Mutex);
        FString Key(UniqueTypeName);
        if (auto It = Tags.find(Key); It != Tags.end())
        {
            return It->second;
        }

        ASSERT(GNextTag < LUA_UTAG_LIMIT, "Out of Luau userdata tags (LUA_UTAG_LIMIT)");
        Tags.emplace(eastl::move(Key), GNextTag);
        return GNextTag++;
    }
}
