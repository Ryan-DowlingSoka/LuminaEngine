#pragma once

#include "Core/Variant/Variant.h"

namespace Lumina::Lua
{
    template<typename T>
    struct TUserdataHeader
    {
        using DecayT = eastl::decay_t<T>;
        using RawT   = eastl::remove_pointer_t<DecayT>;

        RawT*                            External = nullptr;
        // Inline value storage (value types only); pointer types use External and leave this inert.
        // Sized by the decayed type, not the pointee, so a forward-declared pointee still compiles.
        alignas(DecayT) unsigned char    Buffer[sizeof(DecayT)];

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

            return std::launder(reinterpret_cast<RawT*>(Buffer));
        }

        void InvokeDtor()
        {
            if constexpr (!eastl::is_trivially_destructible_v<RawT>)
            {
                if (External)
                {
                    return;
                }
                std::launder(reinterpret_cast<RawT*>(Buffer))->~RawT();
            }
        }
    };
}
