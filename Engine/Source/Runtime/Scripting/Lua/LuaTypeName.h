#pragma once

#include "Traits.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Math/Vector/VectorTypes.h"
#include "Core/Math/Quat/Quat.h"
#include "entt/entt.hpp"

#include <EASTL/type_traits.h>
#include <EASTL/tuple.h>
#include <EASTL/utility.h>

// Compile-time map from a C++ binding type to its Luau static type name, used to auto-generate the
// editor type definitions from the bindings themselves (no parallel hand-written type text). The
// default is "any" -- an unmapped type just loses precision in autocomplete, it never breaks the defs.
namespace Lumina::Lua
{
    template<typename T> struct TLuaTypeName                  { static constexpr const char* Value = "any"; };

    template<> struct TLuaTypeName<bool>                      { static constexpr const char* Value = "boolean"; };
    template<> struct TLuaTypeName<int8>                      { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<int16>                     { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<int32>                     { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<int64>                     { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<uint8>                     { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<uint16>                    { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<uint32>                    { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<uint64>                    { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<float>                     { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<double>                    { static constexpr const char* Value = "number"; };
    template<> struct TLuaTypeName<FString>                   { static constexpr const char* Value = "string"; };
    template<> struct TLuaTypeName<FStringView>              { static constexpr const char* Value = "string"; };
    template<> struct TLuaTypeName<FName>                     { static constexpr const char* Value = "string"; };
    template<> struct TLuaTypeName<entt::entity>             { static constexpr const char* Value = "number"; };

    template<typename U, int N> struct TLuaTypeName<TVec<U, N>> { static constexpr const char* Value = "vector"; };
    template<typename U>        struct TLuaTypeName<TQuat<U>>   { static constexpr const char* Value = "any"; };

    template<typename T>
    constexpr const char* LuaTypeNameOf() { return TLuaTypeName<eastl::remove_cvref_t<T>>::Value; }

    namespace Internal
    {
        inline void AppendUInt(FString& Out, size_t Value)
        {
            char Tmp[20];
            int Len = 0;
            if (Value == 0) { Out += '0'; return; }
            while (Value) { Tmp[Len++] = char('0' + (Value % 10)); Value /= 10; }
            while (Len) { Out += Tmp[--Len]; }
        }

        template<typename Tuple, size_t... I>
        void AppendArgList(FString& Out, eastl::index_sequence<I...>)
        {
            // ", pN: <type>" for each bound argument (after the implicit self).
            ((Out += ", p", AppendUInt(Out, I), Out += ": ",
              Out += LuaTypeNameOf<eastl::tuple_element_t<I, Tuple>>()), ...);
        }
    }

    // Builds a Luau table-field declaration for a method bound from C++ function `TFunc`:
    //   "AddForce: (self: PhysicsScene, p0: number, p1: vector) -> ()"
    // The C++ member-function args map to the Lua args after the colon-call self.
    template<auto TFunc>
    FString BuildLuauMethodDecl(FStringView MethodName, FStringView OwnerTypeName)
    {
        using Traits = TFunctionTraits<decltype(TFunc)>;
        using Args   = typename Traits::ArgsTuple;
        using Ret    = typename Traits::ReturnType;

        FString Decl(MethodName.data(), MethodName.size());
        Decl += ": (self: ";
        Decl.append(OwnerTypeName.data(), OwnerTypeName.size());
        Internal::AppendArgList<Args>(Decl, eastl::make_index_sequence<Traits::ArgCount>{});
        Decl += ") -> ";
        if constexpr (eastl::is_void_v<Ret>) { Decl += "()"; }
        else                                 { Decl += LuaTypeNameOf<Ret>(); }
        return Decl;
    }
}
