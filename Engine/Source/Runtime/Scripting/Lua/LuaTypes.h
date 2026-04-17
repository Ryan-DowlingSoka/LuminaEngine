#pragma once
#include "Platform/GenericPlatform.h"

namespace Lumina::Lua
{
    enum class EType : uint8
    {
        Nil = 0,
        Boolean = 1,
        LightUserData = 2,
        Number = 3,
        Vector = 4,
        String = 5,
        Table = 6,
        Function = 7,
        Userdata = 8,
        Thread = 9,
        Buffer = 10,
        
        Count = 11,
    };
}
