#pragma once

#include "Core/Variant/Variant.h"

namespace Lumina::Lua
{
    // Layout note: External lives at offset 0 of every TUserdataHeader<T>
    // instantiation. That lets a parent-class accessor (whose static type is
    // Parent) read the External pointer of a child userdata block — even
    // though the child's `Buffer[sizeof(Child)]` would otherwise push the
    // parent's view of External to the wrong offset. Inheritance dispatch
    // for properties and namecalls relies on this guarantee.
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
