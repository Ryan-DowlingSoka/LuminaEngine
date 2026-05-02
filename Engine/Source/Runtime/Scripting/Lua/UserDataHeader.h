#pragma once

#include "Core/Variant/Variant.h"

namespace Lumina::Lua
{
    // External at offset 0 across all TUserdataHeader<T> so parent accessors can read it from child blocks.
    // Inheritance dispatch (properties + namecalls) depends on this layout guarantee.
    template<typename T>
    struct TUserdataHeader
    {
        using RawT = eastl::remove_pointer_t<eastl::decay_t<T>>;

        RawT*                          External = nullptr;
        alignas(RawT) unsigned char    Buffer[sizeof(RawT)];

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
            if constexpr (!eastl::is_trivially_destructible_v<RawT>)
            {
                if (External)
                {
                    return;
                }
                reinterpret_cast<RawT*>(Buffer)->~RawT();
            }
        }
    };
}
