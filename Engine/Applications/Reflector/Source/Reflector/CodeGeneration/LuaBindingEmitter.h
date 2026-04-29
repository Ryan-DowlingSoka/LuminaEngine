#pragma once
#include <EASTL/string.h>

namespace Lumina::Reflection
{
    class FReflectedEnum;
    class FReflectedStruct;
    class FReflectedClass;
    class FCodeWriter;

    /**
     * Emits the `SetupLuaBindings(lua_State*)` free function for each reflected type.
     *
     * These are the big hand-authored blocks that used to live inline inside
     * ReflectedType.cpp. Keeping them here means Lua-specific emission concerns
     * are isolated from the "what does a CStruct look like in C++ land" code.
     */
    namespace LuaBindingEmitter
    {
        void EmitForEnum(FCodeWriter& Writer, const FReflectedEnum& Enum);
        void EmitForStruct(FCodeWriter& Writer, const FReflectedStruct& Struct);
        void EmitForClass(FCodeWriter& Writer, const FReflectedClass& Class);
    }
}
