#include <gtest/gtest.h>
#include "lua.h"
#include "luacode.h"
#include "Core/Math/Hash/Hash.h"
#include "Memory/Memory.h"
#include "Platform/GenericPlatform.h"
#include "Scripting/Lua/Reference.h"

using namespace Lumina;

static int16 AtomString(lua_State* L, const char* Str, size_t Length)
{
    return static_cast<int16>(Hash::FNV1a::GetHash16(Str)); 
}

static void* ScriptingMemoryReallocFn([[maybe_unused]] void* Caller, void* Memory, [[maybe_unused]] size_t OldSize, size_t NewSize)
{
    if (NewSize == 0)
    {
        Memory::Free(Memory);
        return nullptr;
    }
        
    return Memory::Realloc(Memory, NewSize);
}

static void RunScript(lua_State* L, const char* Script)
{
    size_t BytecodeSize = 0;
    lua_CompileOptions Options{};
    char* Bytecode = luau_compile(
        Script,
        strlen(Script),
        &Options,
        &BytecodeSize
    );

    ASSERT_NE(Bytecode, nullptr);
    

    int LoadResult = luau_load(
        L,
        "=test",
        Bytecode,
        BytecodeSize,
        0
    );

    free(Bytecode);

    ASSERT_EQ(LoadResult, LUA_OK) << lua_tostring(L, -1);

    int CallResult = lua_pcall(L, 0, 1, 0);

    ASSERT_EQ(CallResult, LUA_OK) << lua_tostring(L, -1);
}

TEST(LuaTests, Class_TableExistsInGlobals)
{
    lua_State* L = lua_newstate(ScriptingMemoryReallocFn, this);

    luaL_openlibs(L);
    
    lua_Callbacks* Callbacks = lua_callbacks(L);
    Callbacks->useratom = AtomString;
    
    {
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        Lua::FRef GlobalsRef(L, -1);

        (void)GlobalsRef.NewTable("Foobar");
    }

    RunScript(L, R"(
        return Foobar
    )");

    ASSERT_TRUE(lua_istable(L, -1));

    lua_close(L);
}

TEST(LuaTests, Class_UserdataExistsInGlobals)
{
    lua_State* L = lua_newstate(ScriptingMemoryReallocFn, this);

    luaL_openlibs(L);
    
    lua_Callbacks* Callbacks = lua_callbacks(L);
    Callbacks->useratom = AtomString;
    
    {
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        Lua::FRef GlobalsRef(L, -1);

        struct FFoobar {};
        GlobalsRef.NewClass<FFoobar>("Foobar").Register();
    }

    RunScript(L, R"(
        return Foobar
    )");

    ASSERT_TRUE(lua_istable(L, -1));

    lua_close(L);
}

TEST(LuaTests, Class_Instance)
{
    lua_State* L = lua_newstate(ScriptingMemoryReallocFn, this);

    luaL_openlibs(L);
    
    lua_Callbacks* Callbacks = lua_callbacks(L);
    Callbacks->useratom = AtomString;
    
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    Lua::FRef GlobalsRef(L, -1);

    struct FFoobar {};
    GlobalsRef.NewClass<FFoobar>("Foobar")
        .Register();

    FFoobar Foo{};
    GlobalsRef.Set("Foo", Foo);
    
    RunScript(L, R"(
        return Foo
    )");

    ASSERT_TRUE(lua_isuserdata(L, -1));

    lua_close(L);
}

TEST(LuaTests, Class_InstanceFunction)
{
    lua_State* L = lua_newstate(ScriptingMemoryReallocFn, this);

    luaL_openlibs(L);
    
    lua_Callbacks* Callbacks = lua_callbacks(L);
    Callbacks->useratom = AtomString;
    
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    Lua::FRef GlobalsRef(L, -1);

    struct FFoobar
    {
        int GetValue() const { return 69; }
    };
    GlobalsRef.NewClass<FFoobar>("Foobar")
        .AddFunction<&FFoobar::GetValue>("GetValue")
        .Register();

    FFoobar Foo{};
    GlobalsRef.Set("Foo", Foo);
    
    RunScript(L, R"(
        return Foo:GetValue()
    )");

    ASSERT_EQ(lua_tointeger(L, -1), 69);

    lua_close(L);
}

TEST(LuaTests, Class_ManyFunctions_DispatchCorrectly)
{
    lua_State* L = lua_newstate(ScriptingMemoryReallocFn, this);
    luaL_openlibs(L);

    lua_Callbacks* Callbacks = lua_callbacks(L);
    Callbacks->useratom = AtomString;

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    Lua::FRef GlobalsRef(L, -1);

    struct FFoobar
    {
        int A() const { return 1; }
        int B() const { return 2; }
        int C() const { return 3; }
        int D() const { return 4; }
        int E() const { return 5; }
        int F() const { return 6; }
        int G() const { return 7; }
        int H() const { return 8; }
        int I() const { return 9; }
        int J() const { return 10; }

        int Sum10() const
        {
            return A() + B() + C() + D() + E() +
                   F() + G() + H() + I() + J();
        }

        int Identity(int v) const
        {
            return v;
        }

        int Mul(int a, int b) const
        {
            return a * b;
        }

        int Chain() const
        {
            return A() + B() * C() + D();
        }
    };

    GlobalsRef
        .NewClass<FFoobar>("Foobar")
        .AddFunction<&FFoobar::A>("A")
        .AddFunction<&FFoobar::B>("B")
        .AddFunction<&FFoobar::C>("C")
        .AddFunction<&FFoobar::D>("D")
        .AddFunction<&FFoobar::E>("E")
        .AddFunction<&FFoobar::F>("F")
        .AddFunction<&FFoobar::G>("G")
        .AddFunction<&FFoobar::H>("H")
        .AddFunction<&FFoobar::I>("I")
        .AddFunction<&FFoobar::J>("J")
        .AddFunction<&FFoobar::Sum10>("Sum10")
        .AddFunction<&FFoobar::Identity>("Identity")
        .AddFunction<&FFoobar::Mul>("Mul")
        .AddFunction<&FFoobar::Chain>("Chain")
        .Register();

    FFoobar Foo{};
    GlobalsRef.Set("Foo", Foo);

    RunScript(L, R"(
        local a = Foo:A()
        local b = Foo:B()
        local c = Foo:C()
        local d = Foo:D()
        local e = Foo:E()
        local f = Foo:F()
        local g = Foo:G()
        local h = Foo:H()
        local i = Foo:I()
        local j = Foo:J()

        assert(a == 1)
        assert(b == 2)
        assert(c == 3)
        assert(d == 4)
        assert(e == 5)
        assert(f == 6)
        assert(g == 7)
        assert(h == 8)
        assert(i == 9)
        assert(j == 10)

        assert(Foo:Sum10() == 55)
        assert(Foo:Identity(42) == 42)
        assert(Foo:Mul(6, 7) == 42)
        assert(Foo:Chain() == 1 + 2 * 3 + 4)

        return Foo:Mul(3, 4)
    )");
    
    ASSERT_EQ(lua_tointeger(L, -1), 12);

    lua_close(L);
}

TEST(LuaTests, Class_FunctionCallLoop)
{
    lua_State* L = lua_newstate(ScriptingMemoryReallocFn, this);

    luaL_openlibs(L);
    
    lua_Callbacks* Callbacks = lua_callbacks(L);
    Callbacks->useratom = AtomString;
    
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    Lua::FRef GlobalsRef(L, -1);

    struct FFoobar
    {
        int GetValue() const { return 1; }
    };
    GlobalsRef.NewClass<FFoobar>("Foobar")
        .AddFunction<&FFoobar::GetValue>("GetValue")
        .Register();

    FFoobar Foo{};
    GlobalsRef.Set("Foo", Foo);
    
    RunScript(L, R"(
        local val = 0
        for i = 1, 100000 do
            val += Foo:GetValue()
        end
        return val
    )");
    
    EXPECT_EQ(lua_tointeger(L, -1), 100000);
    
    lua_close(L);
}