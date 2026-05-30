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

        // Emits the chained TClass builder calls up to but not including .Register();
        // the caller closes the chain so it stays a single stack-balanced expression.
        void EmitTypeBindings(FCodeWriter& Writer, const FReflectedStruct& Struct)
        {
            Writer.Line(".EnableTypeId()");

            if (!Struct.Parent.empty())
            {
                Writer.Linef(".SetSuperClass(\"%s\")", Struct.Parent.c_str());
            }

            for (const auto& Func : Struct.Functions)
            {
                Writer.Linef(".AddFunction<&%s::%s>(\"%s\")",
                    Struct.QualifiedName.c_str(), Func->Name.c_str(), Func->Name.c_str());
            }

            for (const auto& Prop : Struct.Props)
            {
                if (Prop->bInner)
                {
                    continue;
                }
                Writer.Linef(".AddProperty<&%s::%s>(\"%s\")",
                    Struct.QualifiedName.c_str(), Prop->Name.c_str(), Prop->Name.c_str());
            }
        }

        // Chains a `Foo.Load(path)` helper; PushCObjectAsActualType gives the result
        // its actual subclass's metatable, not the static type LoadObject was templated on.
        void EmitClassLoader(FCodeWriter& Writer, const FReflectedStruct& Class)
        {
            Writer.Line(".AddStaticRawFunction(\"Load\", +[](lua_State* State) -> int");
            Writer.BeginBlock();
            Writer.Line("auto Name = lua_tostring(State, 1);");
            Writer.Linef("auto* Instance = Lumina::LoadObject<%s>(Name);", Class.QualifiedName.c_str());
            Writer.Line("Lumina::Lua::PushCObjectAsActualType(State, Instance);");
            Writer.Line("return 1;");
            Writer.EndBlock();
            Writer.Line(");");
        }

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

            // Single chained expression: construct, register methods + props, Register()
            // to commit the metatable, then attach the constructor/loader to the global table.
            Writer.Linef("Lumina::Lua::TClass<%s>(L, \"%s\")",
                Struct.QualifiedName.c_str(), Struct.DisplayName.c_str());
            Writer.PushIndent();

            EmitTypeBindings(Writer, Struct);
            Writer.Line(".Register()");

            if (bIsClass)
            {
                EmitClassLoader(Writer, Struct);
            }
            else
            {
                Writer.Linef(".AddConstructor<&Lumina::Lua::DefaultConstruct<%s>>();",
                    Struct.QualifiedName.c_str());
            }
            Writer.PopIndent();

            Writer.Line("DEBUG_ASSERT(BindingTop == lua_gettop(L));");
            Writer.EndBlock();
        }

    }

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
}
