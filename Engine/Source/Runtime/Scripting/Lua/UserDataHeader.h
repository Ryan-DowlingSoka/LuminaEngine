#pragma once

#include "Core/Variant/Variant.h"

namespace Lumina::Lua
{
    template<typename T>
    struct TUserdataHeader
    {
        using RawT = eastl::remove_pointer_t<eastl::decay_t<T>>;
        
        template<typename... TArgs>
        void Emplace(TArgs&&... Args)
        {
            new(Buffer) RawT(eastl::forward<TArgs>(Args)...);
        }

        void SetExternal(RawT* Ptr)
        {
            External = Ptr;
        }

        RawT* Underlying()
        {
            if (External)
            {
                return External;
            }
            
            return reinterpret_cast<RawT*>(Buffer);
        }
        
        void InvokeDtor()
        {
            //Storage = {};
        }
        
        alignas(RawT) unsigned char Buffer[sizeof(RawT)];
        RawT* External = nullptr;
    };
}