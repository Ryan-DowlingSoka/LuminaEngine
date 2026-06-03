#include "pch.h"
#include "Traits.h"
#include "Core/Assertions/Assert.h"
#include "Containers/Array.h"
#include "Core/Threading/Thread.h"

namespace Lumina::Lua
{
    uint16 FTypeIndex::Next()
    {
        static uint16 GNextID = 1;
        DEBUG_ASSERT(GNextID <= eastl::numeric_limits<uint16>::max(), "Hit the maximum number of allowed userdata!");
        return GNextID++;
    }

    uint16 FTypeIndex::GetOrCreate(uint64 TypeKey)
    {
        static FMutex Mutex;
        static THashMap<uint64, uint16> Tags;

        FScopeLock Lock(Mutex);
        if (auto It = Tags.find(TypeKey); It != Tags.end())
        {
            return It->second;
        }

        uint16 Tag = Next();
        Tags.emplace(TypeKey, Tag);
        return Tag;
    }
}
