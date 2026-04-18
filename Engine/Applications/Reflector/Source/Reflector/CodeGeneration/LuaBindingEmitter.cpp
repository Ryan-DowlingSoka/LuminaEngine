#include "LuaBindingEmitter.h"

#include <EASTL/algorithm.h>

#include "CodeWriter.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/Types/Functions/ReflectedFunction.h"
#include "Reflector/Types/Properties/ReflectedProperty.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina::Reflection
{
    namespace
    {
        bool HasNoLuaMetadata(const FReflectedType& Type)
        {
            return eastl::any_of(Type.Metadata.begin(), Type.Metadata.end(),
                [](const FMetadataPair& Pair) { return Pair.Key == "NoLua"; });
        }

        // __namecall: dispatches Lua self:Foo() syntax via lua_namecallatom.
        void EmitNamecallMetamethod(FCodeWriter& Writer, const FReflectedStruct& Struct)
        {
            if (Struct.Functions.empty())
            {
                return;
            }

            Writer.Line("lua_pushcfunction(L, +[](lua_State* VM) -> int");
            Writer.BeginBlock();
            Writer.Line("int Atom = 0;");
            Writer.Line("lua_namecallatom(VM, &Atom);");
            Writer.Line("switch((uint16)Atom)");
            Writer.BeginBlock();
            Writer.PopIndent();

            for (const auto& Func : Struct.Functions)
            {
                Writer.Linef("case(Lumina::Hash::FNV1a::GetHash16(\"%s\")): return Lumina::Lua::Invoker<&%s::%s>(VM);",
                    Func->Name.c_str(), Struct.QualifiedName.c_str(), Func->Name.c_str());
            }
            Writer.Line("default: return 0;");

            Writer.PushIndent();
            Writer.EndBlock();
            Writer.PopIndent();
            Writer.Line("}, \"__namecall\");");
            Writer.PushIndent();
            Writer.Line("lua_rawsetfield(L, MetaTableIdx, \"__namecall\");");
        }

        // __index: reads property values (and __type_id) back out to Lua.
        void EmitIndexMetamethod(FCodeWriter& Writer, const FReflectedStruct& Struct)
        {
            if (Struct.Props.empty())
            {
                return;
            }

            Writer.Line("lua_pushcfunction(L, +[](lua_State* VM) -> int");
            Writer.BeginBlock();
            Writer.Linef("if(!Lumina::Lua::TStack<%s*>::Check(VM, 1)) return 0;", Struct.QualifiedName.c_str());
            Writer.Linef("%s* ThisType = Lumina::Lua::TStack<%s*>::Get(VM, 1);",
                Struct.QualifiedName.c_str(), Struct.QualifiedName.c_str());
            Writer.Line("const char* Key = lua_tostring(VM, 2);");
            Writer.Line("uint32 Hash = Lumina::Hash::FNV1a::GetHash32(Key);");
            Writer.Line("switch(Hash)");
            Writer.BeginBlock();
            Writer.PopIndent();

            Writer.Line("case(Lumina::Hash::FNV1a::GetHash32(\"__type_id\")):");
            Writer.BeginBlock();
            Writer.Linef("Lumina::Lua::TStack<uint32>::Push(VM, entt::hashed_string(\"%s\"));", Struct.DisplayName.c_str());
            Writer.Line("break;");
            Writer.EndBlock();

            for (const auto& Prop : Struct.Props)
            {
                if (Prop->bInner)
                {
                    continue;
                }
                Writer.Linef("case(Lumina::Hash::FNV1a::GetHash32(\"%s\")): Lumina::Lua::TStack<decltype(%s::%s)>::Push(VM, ThisType->%s); break;",
                    Prop->Name.c_str(), Struct.QualifiedName.c_str(), Prop->Name.c_str(), Prop->Name.c_str());
            }

            Writer.Line("default: return 0;");
            Writer.PushIndent();
            Writer.EndBlock();

            Writer.Line("return 1;");
            Writer.PopIndent();
            Writer.Line("}, \"__index\");");
            Writer.PushIndent();
            Writer.Line("lua_rawsetfield(L, MetaTableIdx, \"__index\");");
        }

        // __newindex: writes property values from Lua back into the C++ struct.
        void EmitNewindexMetamethod(FCodeWriter& Writer, const FReflectedStruct& Struct)
        {
            if (Struct.Props.empty())
            {
                return;
            }

            Writer.Line("lua_pushcfunction(L, +[](lua_State* VM) -> int");
            Writer.BeginBlock();
            Writer.Linef("if(!Lumina::Lua::TStack<%s*>::Check(VM, 1)) return 0;", Struct.QualifiedName.c_str());
            Writer.Linef("%s* ThisType = Lumina::Lua::TStack<%s*>::Get(VM, 1);",
                Struct.QualifiedName.c_str(), Struct.QualifiedName.c_str());
            Writer.Line("const char* Key = lua_tostring(VM, 2);");
            Writer.Line("uint32 Hash = Lumina::Hash::FNV1a::GetHash32(Key);");
            Writer.Line("switch(Hash)");
            Writer.BeginBlock();
            Writer.PopIndent();

            for (const auto& Prop : Struct.Props)
            {
                if (Prop->bInner)
                {
                    continue;
                }
                Writer.Linef("case(Lumina::Hash::FNV1a::GetHash32(\"%s\")):", Prop->Name.c_str());
                Writer.BeginBlock();
                Writer.Linef("ThisType->%s = Lumina::Lua::TStack<decltype(%s::%s)>::Get(VM, 3);",
                    Prop->Name.c_str(), Struct.QualifiedName.c_str(), Prop->Name.c_str());
                Writer.Line("break;");
                Writer.EndBlock();
            }

            Writer.Line("default: break;");
            Writer.PushIndent();
            Writer.EndBlock();

            Writer.Line("return 0;");
            Writer.PopIndent();
            Writer.Line("}, \"__newindex\");");
            Writer.PushIndent();
            Writer.Line("lua_rawsetfield(L, MetaTableIdx, \"__newindex\");");
        }

        // Per-struct `new` constructor exposed to Lua.
        void EmitStructConstructor(FCodeWriter& Writer, const FReflectedStruct& Struct)
        {
            Writer.Line("lua_pushcfunction(L, +[](lua_State* State)");
            Writer.BeginBlock();
            Writer.Linef("void* Block = lua_newuserdatataggedwithmetatable(State, sizeof(Lumina::Lua::TUserdataHeader<%s>), Lumina::Lua::TClassTraits<%s>::Tag());",
                Struct.QualifiedName.c_str(), Struct.QualifiedName.c_str());
            Writer.Linef("auto* Header = new (Block) Lumina::Lua::TUserdataHeader<%s>{};", Struct.QualifiedName.c_str());
            Writer.Linef("auto Instance = %s{};", Struct.QualifiedName.c_str());
            Writer.Line("Header->Emplace(Instance);");
            Writer.Line("return 1;");
            Writer.PopIndent();
            Writer.Line("}, \"new\");");
            Writer.PushIndent();
            Writer.Line("lua_rawsetfield(L, -2, \"new\");");
        }

        // Per-class `Load(name)` helper exposed to Lua.
        void EmitClassLoader(FCodeWriter& Writer, const FReflectedStruct& Class)
        {
            Writer.Line("lua_pushcfunction(L, +[](lua_State* State)");
            Writer.BeginBlock();
            Writer.Line("auto Name = lua_tostring(State, 1);");
            Writer.Linef("auto* Instance = Lumina::LoadObject<%s>(Name);", Class.QualifiedName.c_str());
            Writer.Line("if(Instance == nullptr)");
            Writer.BeginBlock();
            Writer.Line("lua_pushnil(State);");
            Writer.EndBlock();
            Writer.Line("else");
            Writer.BeginBlock();
            Writer.Linef("void* Block = lua_newuserdatataggedwithmetatable(State, sizeof(Lumina::Lua::TUserdataHeader<%s>), Lumina::Lua::TClassTraits<%s>::Tag());",
                Class.QualifiedName.c_str(), Class.QualifiedName.c_str());
            Writer.Linef("auto* Header = new (Block) Lumina::Lua::TUserdataHeader<%s>{};", Class.QualifiedName.c_str());
            Writer.Line("Header->SetExternal(Instance);");
            Writer.EndBlock();
            Writer.Line("return 1;");
            Writer.PopIndent();
            Writer.Line("}, \"Load\");");
            Writer.PushIndent();
            Writer.Line("lua_rawsetfield(L, -2, \"Load\");");
        }

        //---------------------------------------------------------------------
        // Shared body between FReflectedStruct and FReflectedClass. The only
        // difference is whether we expose `new` (struct) or `Load` (class).
        void EmitStructlikeBody(FCodeWriter& Writer, const FReflectedStruct& Struct, bool bIsClass)
        {
            Writer.Line("static void SetupLuaBindings(lua_State* L)");
            Writer.BeginBlock();

            if (HasNoLuaMetadata(Struct))
            {
                Writer.EndBlock();
                return;
            }

            Writer.Line("int BindingTop = lua_gettop(L);");
            Writer.Linef("luaL_newmetatable(L, \"%s\");", Struct.DisplayName.c_str());
            Writer.Line("int MetaTableIdx = lua_gettop(L);");

            EmitNamecallMetamethod(Writer, Struct);
            EmitIndexMetamethod(Writer, Struct);
            EmitNewindexMetamethod(Writer, Struct);

            Writer.Linef("lua_setuserdatametatable(L, Lumina::Lua::TClassTraits<%s>::Tag());", Struct.QualifiedName.c_str());

            Writer.Line("lua_newtable(L);");
            Writer.Linef("lua_pushunsigned(L, entt::hashed_string(\"%s\"));", Struct.DisplayName.c_str());
            Writer.Line("lua_rawsetfield(L, MetaTableIdx, \"__type_id\");");

            if (bIsClass)
            {
                EmitClassLoader(Writer, Struct);
            }
            else
            {
                EmitStructConstructor(Writer, Struct);
            }

            Writer.Linef("lua_setglobal(L, \"%s\");", Struct.DisplayName.c_str());
            Writer.Line("DEBUG_ASSERT(BindingTop == lua_gettop(L));");
            Writer.EndBlock();
        }

        //---------------------------------------------------------------------
        // Lua type name for a single field. "any" when we can't pin it down.
        const char* LuaTypeForFlags(EPropertyTypeFlags Flags)
        {
            switch (Flags)
            {
            case EPropertyTypeFlags::Int8:
            case EPropertyTypeFlags::Int16:
            case EPropertyTypeFlags::Int32:
            case EPropertyTypeFlags::Int64:
            case EPropertyTypeFlags::UInt8:
            case EPropertyTypeFlags::UInt16:
            case EPropertyTypeFlags::UInt32:
            case EPropertyTypeFlags::UInt64:
            case EPropertyTypeFlags::Float:
            case EPropertyTypeFlags::Double:
            case EPropertyTypeFlags::Enum:
                return "number";
            case EPropertyTypeFlags::Bool:
                return "boolean";
            case EPropertyTypeFlags::Name:
            case EPropertyTypeFlags::String:
                return "string";
            case EPropertyTypeFlags::Object:
            case EPropertyTypeFlags::Class:
            case EPropertyTypeFlags::Struct:
                return nullptr; // caller should fall back to the stripped type name
            case EPropertyTypeFlags::None:
            case EPropertyTypeFlags::Vector:
                return "any";
            }
            return "any";
        }

        eastl::string LuaTypeForField(const FFieldInfo& Field)
        {
            if (const char* Simple = LuaTypeForFlags(Field.Flags))
            {
                return Simple;
            }
            return ClangUtils::StripNamespace(Field.TypeName);
        }

        void AppendFunctionSignature(FCodeWriter& Writer, const FReflectedFunction& Function)
        {
            if (Function.Arguments.empty())
            {
                Writer.Linef("\tfunction %s(self): any", Function.Name.c_str());
                return;
            }

            eastl::string Line = "\tfunction ";
            Line += Function.Name;
            Line += "(self";

            for (const FFieldInfo& Arg : Function.Arguments)
            {
                Line += ", ";
                Line += Arg.Name;
                Line += ": ";
                Line += LuaTypeForField(Arg);
            }

            Line += ")";

            if (Function.Return.has_value())
            {
                Line += ": ";
                Line += LuaTypeForField(*Function.Return);
            }

            Writer.Line(Line);
        }
    }

    //-------------------------------------------------------------------------
    // LuaBindingEmitter
    //-------------------------------------------------------------------------

    void LuaBindingEmitter::EmitForEnum(FCodeWriter& Writer, const FReflectedEnum& Enum)
    {
        Writer.Line("static void SetupLuaBindings(lua_State* L)");
        Writer.BeginBlock();
        Writer.Line("lua_newtable(L);");

        for (const FReflectedEnum::FConstant& Constant : Enum.Constants)
        {
            Writer.Linef("lua_pushinteger(L, static_cast<int>(%s::%s));", Enum.QualifiedName.c_str(), Constant.Label.c_str());
            Writer.Linef("lua_rawsetfield(L, -2, \"%s\");", Constant.Label.c_str());
        }

        Writer.Linef("lua_setglobal(L, \"%s\");", Enum.DisplayName.c_str());
        Writer.EndBlock();
    }

    void LuaBindingEmitter::EmitForStruct(FCodeWriter& Writer, const FReflectedStruct& Struct)
    {
        EmitStructlikeBody(Writer, Struct, /*bIsClass*/ false);
    }

    void LuaBindingEmitter::EmitForClass(FCodeWriter& Writer, const FReflectedClass& Class)
    {
        EmitStructlikeBody(Writer, Class, /*bIsClass*/ true);
    }

    //-------------------------------------------------------------------------
    // LuaApiEmitter
    //-------------------------------------------------------------------------

    void LuaApiEmitter::EmitForEnum(FCodeWriter& Writer, const FReflectedEnum& Enum)
    {
        Writer.Linef("declare %s: number", Enum.DisplayName.c_str());
    }

    void LuaApiEmitter::EmitForStruct(FCodeWriter& Writer, const FReflectedStruct& Struct)
    {
        Writer.Linef("declare %s: { }", Struct.DisplayName.c_str());
        Writer.Linef("declare class %s", Struct.DisplayName.c_str());

        for (const auto& Prop : Struct.Props)
        {
            if (Prop->bInner || Prop->GetLuaType().empty())
            {
                continue;
            }

            Writer.Linef("\t%s: %s", Prop->Name.c_str(), eastl::string(Prop->GetLuaType()).c_str());
        }

        for (const auto& Function : Struct.Functions)
        {
            AppendFunctionSignature(Writer, *Function);
        }

        Writer.Line("end");
    }
}
