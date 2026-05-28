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
        // Inline value storage, used only when T is a value type. For pointer storage the External
        // path is used and this buffer is inert, so it's sized by the decayed type (pointer-sized)
        // rather than the pointee -- otherwise a forward-declared pointee would fail to compile.
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
