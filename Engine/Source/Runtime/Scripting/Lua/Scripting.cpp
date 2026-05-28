#include "pch.h"
#include "Scripting.h"
#include "Core/Math/Math.h"
#include "lstate.h"
#include "luacode.h"
#include "lualib.h"
#include "ScriptTypes.h"
#include "Debugger/LuaDebugger.h"
#include "Audio/AudioGlobals.h"
#include "Core/Utils/TimedEvent.h"
#include "Events/KeyCodes.h"
#include "FileSystem/FileSystem.h"
#include "Input/InputActionMap.h"
#include "Input/InputContext.h"
#include "Input/InputProcessor.h"
#include "Input/InputViewport.h"
#include "Luau/include/lua.h"
#include "Memory/SmartPtr.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "TaskSystem/ThreadedCallback.h"
#include "World/World.h"
#include "World/Entity/RuntimeComponent.h"
#include "UI/RmlUiBridge.h"
#include "Core/Application/Application.h"
#include "Core/Object/Class.h"
#include "Events/EventProcessor.h"
#include "Input/InputMode.h"
#include "Memory/MemoryTracking.h"
#include "World/Entity/Systems/NavMeshSystem.h"
#include "World/Entity/Systems/SystemContext.h"

namespace Lumina::Lua
{
    FClassBuilder::FClassBuilder(lua_State* InL, FStringView InName)
        : L(InL)
        , Name(InName)
    {
        luaL_newmetatable(L, InName.data()); // [MT]

        lua_pushstring(L, InName.data());
        lua_rawsetfield(L, -2, "__typename");
    }

    FClassBuilder& FClassBuilder::SetSuperClass(FStringView InParentName)
    {
        ParentName = InParentName;
        return *this;
    }

    FClassBuilder& FClassBuilder::EnableTypeId()
    {
        bHasTypeId = true;
        return *this;
    }

    FClassBuilder& FClassBuilder::AddMethod(FStringView FuncName, lua_CFunction Func)
    {
        FMethodEntry Entry;
        Entry.Name   = FuncName;
        Entry.Invoke = Func;
        Methods.push_back(Entry);
        return *this;
    }

    FClassBuilder& FClassBuilder::AddProperty(FStringView PropName, lua_CFunction Getter, lua_CFunction Setter)
    {
        FPropertyEntry Entry;
        Entry.Name   = PropName;
        Entry.Getter = Getter;
        Entry.Setter = Setter;
        Properties.push_back(Entry);
        return *this;
    }

    FClassBuilder& FClassBuilder::AddMetamethod(FStringView MetaName, lua_CFunction Func)
    {
        // Stack on entry: [MT]
        lua_pushcfunction(L, Func, MetaName.data()); // [MT, func]
        lua_rawsetfield(L, -2, MetaName.data());     // [MT]
        return *this;
    }

    FClassBuilder& FClassBuilder::Register(int UserdataTag)
    {
        // Stack on entry: [MT]

        const uint32 TypeIdHash = bHasTypeId ? Hash::FNV1a::GetHash32(Name.data()) : 0u;

        for (auto& Method : Methods)
        {
            Method.Atom = static_cast<int16>(Hash::FNV1a::GetHash16(Method.Name.data()));
        }
        for (auto& Prop   : Properties)
        {
            Prop.Hash   = Hash::FNV1a::GetHash32(Prop.Name.data());
        }

        // Single hop captures the full ancestry; parent already merged its own.
        TVector<FMethodEntry>   MergedMethods = Methods;
        TVector<FPropertyEntry> MergedProps   = Properties;

        if (!ParentName.empty())
        {
            luaL_getmetatable(L, ParentName.data()); // [MT, ParentMT|nil]
            if (lua_istable(L, -1))
            {
                lua_rawgetfield(L, -1, "__lumina_methods"); // [MT, ParentMT, ParentMethods|nil]
                if (lua_isuserdata(L, -1))
                {
                    const auto* ParentTable = static_cast<const Internal::FEntryTable<FMethodEntry>*>(lua_touserdata(L, -1));
                    const FMethodEntry* B = ParentTable->Entries();
                    const FMethodEntry* E = B + ParentTable->Count;
                    for (const FMethodEntry* It = B; It != E; ++It)
                    {
                        const bool bExists = eastl::any_of(MergedMethods.begin(), MergedMethods.end(),[&](const FMethodEntry& Existing)
                        {
                            return Existing.Name == It->Name;
                        });
                        
                        if (!bExists)
                        {
                            MergedMethods.push_back(*It);
                        }
                    }
                }
                lua_pop(L, 1); // [MT, ParentMT]

                lua_rawgetfield(L, -1, "__lumina_properties"); // [MT, ParentMT, ParentProps|nil]
                if (lua_isuserdata(L, -1))
                {
                    const auto* ParentTable = static_cast<const Internal::FEntryTable<FPropertyEntry>*>(lua_touserdata(L, -1));
                    const FPropertyEntry* B = ParentTable->Entries();
                    const FPropertyEntry* E = B + ParentTable->Count;
                    for (const FPropertyEntry* It = B; It != E; ++It)
                    {
                        const bool bExists = eastl::any_of(MergedProps.begin(), MergedProps.end(), [&](const FPropertyEntry& Existing)
                        {
                            return Existing.Name == It->Name;
                        });
                        
                        if (!bExists)
                        {
                            MergedProps.push_back(*It);
                        }
                    }
                }
                lua_pop(L, 1); // [MT, ParentMT]
            }
            lua_pop(L, 1); // [MT]
        }

        if (bHasTypeId)
        {
            // Synthetic __type_id reads from MT at access time; keeps the dispatcher upvalue free.
            FPropertyEntry TypeIdProp;
            TypeIdProp.Name   = FStringView("__type_id");
            TypeIdProp.Hash   = Hash::FNV1a::GetHash32("__type_id");
            TypeIdProp.Getter = +[](lua_State* State) -> int
            {
                if (!lua_getmetatable(State, 1))
                {
                    lua_pushnil(State); return 1;
                }
                
                lua_rawgetfield(State, -1, "__type_id");
                lua_remove(State, -2);
                return 1;
            };
            MergedProps.erase(
                eastl::remove_if(MergedProps.begin(), MergedProps.end(), [](const FPropertyEntry& E)
                {
                    return E.Name == FStringView("__type_id");
                }), MergedProps.end());
            
            
            MergedProps.push_back(TypeIdProp);
        }

        eastl::sort(MergedMethods.begin(), MergedMethods.end(), [](const FMethodEntry& A, const FMethodEntry& B)
        {
            return A.Atom < B.Atom;
        });
        
        eastl::sort(MergedProps.begin(), MergedProps.end(), [](const FPropertyEntry& A, const FPropertyEntry& B)
        {
            return A.Hash < B.Hash;
        });

        if (!MergedMethods.empty())
        {
            auto* Stored = Internal::FEntryTable<FMethodEntry>::Allocate(L, static_cast<uint32>(MergedMethods.size()));
            for (uint32 i = 0; i < MergedMethods.size(); ++i)
            {
                Stored->Entries()[i] = MergedMethods[i];
            }

            lua_pushvalue(L, -1); // [MT, MethodsUD, MethodsUD]
            lua_rawsetfield(L, -3, "__lumina_methods"); // [MT, MethodsUD]

            lua_pushcclosure(L, &Internal::GenericNamecall, "__namecall", 1); // [MT, NamecallClosure]
            lua_rawsetfield(L, -2, "__namecall"); // [MT]
        }

        if (bHasTypeId || !MergedProps.empty())
        {
            auto* PropsUD = Internal::FEntryTable<FPropertyEntry>::Allocate(L, static_cast<uint32>(MergedProps.size())); // [MT, PropsUD]
            for (uint32 i = 0; i < MergedProps.size(); ++i)
            {
                PropsUD->Entries()[i] = MergedProps[i];
            }

            lua_pushvalue(L, -1); // [MT, PropsUD, PropsUD]
            lua_rawsetfield(L, -3, "__lumina_properties"); // [MT, PropsUD]

            lua_pushvalue(L, -1); // [MT, PropsUD, PropsUD]
            lua_pushcclosure(L, &Internal::GenericIndex, "__index", 1); // [MT, PropsUD, IndexClosure]
            lua_rawsetfield(L, -3, "__index"); // [MT, PropsUD]

            lua_pushcclosure(L, &Internal::GenericNewindex, "__newindex", 1); // [MT, NewindexClosure]
            lua_rawsetfield(L, -2, "__newindex"); // [MT]
        }

        // Editor introspection table: name -> "method"|"property".
        lua_newtable(L); // [MT, MembersTable]
        for (const auto& M : MergedMethods)
        {
            lua_pushlstring(L, "method", 6);
            lua_rawsetfield(L, -2, M.Name.data());
        }
        for (const auto& P : MergedProps)
        {
            lua_pushlstring(L, "property", 8);
            lua_rawsetfield(L, -2, P.Name.data());
        }
        if (!ParentName.empty())
        {
            lua_pushlstring(L, ParentName.data(), ParentName.size());
            lua_rawsetfield(L, -2, "__parentname");
        }
        lua_rawsetfield(L, -2, "__lumina_members"); // [MT]

        if (bHasTypeId)
        {
            lua_pushunsigned(L, TypeIdHash);
            lua_rawsetfield(L, -2, "__type_id"); // [MT]
        }

        InstallUserdataDestructor(UserdataTag);

        lua_pushvalue(L, -1); // [MT, MTcopy]
        lua_setuserdatametatable(L, UserdataTag); // [MT]

        lua_newtable(L); // [MT, GlobalTable]
        if (bHasTypeId)
        {
            lua_pushunsigned(L, TypeIdHash);
            lua_rawsetfield(L, -2, "__type_id");
        }
        lua_pushstring(L, Name.data());
        lua_rawsetfield(L, -2, "__typename");
        lua_setglobal(L, Name.data()); // [MT]

        lua_pop(L, 1); // []

        return *this;
    }

    FClassBuilder& FClassBuilder::AddStaticFunction(FStringView FuncName, lua_CFunction Func)
    {
        lua_getglobal(L, Name.data());
        lua_pushcfunction(L, Func, FuncName.data());
        lua_rawsetfield(L, -2, FuncName.data());
        lua_pop(L, 1);
        return *this;
    }

    namespace
    {
        // Keyed by CClass*; lifetime is process-wide (CClass is leaked by design).
        THashMap<const CClass*, FUserdataLayout>& GetCObjectLayoutRegistry()
        {
            static THashMap<const CClass*, FUserdataLayout> Registry;
            return Registry;
        }
    }

    void RegisterCObjectLayout(const CClass* Class, const FUserdataLayout& Layout)
    {
        if (Class == nullptr)
        {
            return;
        }
        GetCObjectLayoutRegistry()[Class] = Layout;
    }

    const FUserdataLayout* FindCObjectLayout(const CClass* Class)
    {
        if (Class == nullptr)
        {
            return nullptr;
        }
        const auto& Registry = GetCObjectLayoutRegistry();
        const auto It = Registry.find(Class);
        return It != Registry.end() ? &It->second : nullptr;
    }

    void PushCObjectAsActualType(lua_State* L, CObject* Object)
    {
        if (Object == nullptr)
        {
            lua_pushnil(L);
            return;
        }

        // Walk up to nearest bound ancestor so unbound subclasses still get a metatable.
        const FUserdataLayout* Layout = nullptr;
        for (const CClass* Class = Object->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
        {
            Layout = FindCObjectLayout(Class);
            if (Layout != nullptr)
            {
                break;
            }
        }

        if (Layout == nullptr)
        {
            lua_pushnil(L);
            return;
        }

        void* Block = lua_newuserdatataggedwithmetatable(L, Layout->Size, Layout->Tag);
        Layout->Initialize(Block);
        // SetExternal constructs the userdata's owning TObjectPtr<ClassT> (takes a strong GC ref);
        // the tag's destructor (TClass::Register) runs ~TObjectPtr to release it.
        Layout->SetExternal(Block, Object);
    }

    static void* ScriptingMemoryReallocFn([[maybe_unused]] void* Caller, void* Memory, [[maybe_unused]] size_t OldSize, size_t NewSize)
    {
        LUMINA_MEMORY_SCOPE("Scripting");

        if (NewSize == 0)
        {
            Memory::Free(Memory);
            return nullptr;
        }
        
        return Memory::Realloc(Memory, NewSize);
    }
    
    static void LuaPanicHandler(lua_State* L, int ErrorCode)
    {
        PANIC("Lua Panic {}", ErrorCode);
    }
    
    static int16 AtomString(lua_State* L, const char* Str, size_t Length)
    {
        return static_cast<int16>(Hash::FNV1a::GetHash16(Str)); 
    }

    enum class ELuaLogLevel : uint8 { Info, Warn, Error };

    static int LuminaLuaLogImpl(lua_State* L, ELuaLogLevel Level)
    {
        const int32 Count = lua_gettop(L);
        FFixedString Output;

        for (int32 Index = 1; Index <= Count; ++Index)
        {
            size_t Length = 0;
            FStringView String = luaL_tolstring(L, Index, &Length);

            if (Index > 1)
            {
                Output.append(" ");
            }

            Output.append_convert(String.begin(), String.length());

            lua_pop(L, 1);
        }

        switch (Level)
        {
            case ELuaLogLevel::Warn:    LOG_WARN ("[Lua] - {}", Output); break;
            case ELuaLogLevel::Error:   LOG_ERROR("[Lua] - {}", Output); break;
            case ELuaLogLevel::Info:
            default:                    LOG_INFO ("[Lua] - {}", Output); break;
        }
        return 0;
    }

    static int LuminaLuaPrint(lua_State* L)    { return LuminaLuaLogImpl(L, ELuaLogLevel::Info);  }
    static int LuminaLuaLogWarn(lua_State* L)  { return LuminaLuaLogImpl(L, ELuaLogLevel::Warn);  }
    static int LuminaLuaLogError(lua_State* L) { return LuminaLuaLogImpl(L, ELuaLogLevel::Error); }

    // C entry point for the global `require`; keeps Luau's sandboxed-thread error path intact.
    static int LuminaLuaRequire(lua_State* L)
    {
        const char* ModName = luaL_checkstring(L, 1);
        if (!FScriptingContext::Get().RequireModule(L, FStringView(ModName)))
        {
            luaL_errorL(L, "require: failed to load module '%s'", ModName);
        }
        return 1;
    }

    static TUniquePtr<FScriptingContext> GScriptingContext;

    namespace { bool CompileSourceToBytecode(FStringView Code, TVector<uint8>& Out, FCompileDiagnostic* OutDiag = nullptr); }

    void Initialize()
    {
        GScriptingContext = MakeUnique<FScriptingContext>();
        GScriptingContext->Initialize();
    }

    void Shutdown()
    {
        GScriptingContext->Shutdown();
        GScriptingContext.reset();
    }

    FScriptingContext& FScriptingContext::Get()
    {
        return *GScriptingContext.get();
    }
    
    // --- Engine script library ------------------------------------------------
    // Each function owns one top-level global table (plus closely related
    // userdata classes) and is invoked once from FScriptingContext::Initialize.
    // Grouping the bind list by domain keeps it readable and gives each engine
    // system an obvious home to grow its script surface (e.g. RHI.* lives in
    // RegisterRenderLibrary).

    static void RegisterConsoleLibrary(lua_State* L, FRef& GlobalsRef)
    {
        // `print` is also aliased to Console.Log globally (see Initialize). These are raw
        // variadic loggers, so they bind as raw cfunctions rather than typed Invokers.
        FRef ConsoleTable = GlobalsRef.NewTable("Console");
        ConsoleTable.SetRawFunction("Log",   LuminaLuaPrint);
        ConsoleTable.SetRawFunction("Warn",  LuminaLuaLogWarn);
        ConsoleTable.SetRawFunction("Error", LuminaLuaLogError);
    }

    static void RegisterTimeLibrary(lua_State* L, FRef& GlobalsRef)
    {
        // Reads from UpdateContext so all scripts see the same frame values.
        FRef TimeTable = GlobalsRef.NewTable("Time");
        TimeTable.SetFunction<[]() -> double { return GEngine ? GEngine->GetUpdateContext().GetDeltaTime() : 0.0; }>("DeltaTime");
        TimeTable.SetFunction<[]() -> double { return GEngine ? GEngine->GetUpdateContext().GetTime() : 0.0; }>("Now");
        TimeTable.SetFunction<[]() -> uint64 { return GEngine ? GEngine->GetUpdateContext().GetFrame() : uint64{0}; }>("FrameNumber");
        TimeTable.SetFunction<[]() -> float  { return GEngine ? GEngine->GetUpdateContext().GetFPS() : 0.0f; }>("FPS");
    }

    static void RegisterEngineLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef EngineTable = GlobalsRef.NewTable("Engine");

        // Returns the loaded object pushed as its actual derived type so subclass methods are
        // visible. TStack pushes a pointer under its static type's metatable, so we do the
        // polymorphic push by hand and hand back the result as an FRef.
        EngineTable.SetFunction<[](lua_State* State, FStringView Path) -> FRef
        {
            CObject* Object = StaticLoadObject(Path);
            PushCObjectAsActualType(State, Object);
            return FRef(State, -1);
        }>("LoadObject");

        EngineTable.SetFunction<&FEngine::GetProjectName>("GetProjectName", GEngine);
        EngineTable.SetFunction<&FEngine::GetProjectPath>("GetProjectPath", GEngine);
        EngineTable.SetFunction<&FEngine::Travel>("Travel", GEngine);
        EngineTable.SetFunction<[]() { FScriptingContext::Get().ReloadStdlib(); }>("ReloadStdlib");

        // Game/editor mode probe. The lua_State is injected (TLuaContext<lua_State*>); the world
        // this script belongs to comes from its thread data, published by SetupScriptComponent.
        EngineTable.SetFunction<[](lua_State* State) -> bool
        {
            const auto* TD = static_cast<FScriptThreadData*>(lua_getthreaddata(State));
            return TD && TD->World && TD->World->IsGameWorld();
        }>("IsGame");
        EngineTable.SetFunction<[](lua_State* State) -> bool
        {
            const auto* TD = static_cast<FScriptThreadData*>(lua_getthreaddata(State));
            return TD && TD->World && TD->World->GetWorldType() == EWorldType::Editor;
        }>("IsEditor");
    }

    static void RegisterFileSystemLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef VFSTable = GlobalsRef.NewTable("VFS");
        VFSTable.SetFunction<&VFS::Exists>("Exists");
        VFSTable.SetFunction<&VFS::CreateDir>("CreateDir");
        VFSTable.SetFunction<&VFS::FileName>("FileName");
        VFSTable.SetFunction<&VFS::IsDirectory>("IsDirectory");
        VFSTable.SetFunction<&VFS::IsEmpty>("IsEmpty");
        VFSTable.SetFunction<&VFS::PlatformOpen>("PlatformOpen");
        VFSTable.SetFunction<&VFS::Rename>("Rename");
        VFSTable.SetFunction<&VFS::Remove>("Remove");
        VFSTable.SetFunction<&VFS::Parent>("Parent");
        VFSTable.SetFunction<&VFS::IsUnderDirectory>("IsUnderDirectory");
        VFSTable.SetFunction<[](FStringView Path)
        {
            FString Data;
            VFS::ReadFile(Data, Path);
            return Data;
        }>("ReadFileString");
        VFSTable.SetFunction<[](FStringView Path)
        {
            TVector<uint8> Data;
            VFS::ReadFile(Data, Path);
            return Data;
        }>("ReadFileArray");
        VFSTable.SetFunction<[](FStringView Path, TVector<uint8> Data)
        {
            return VFS::WriteFile(Path, Data);
        }>("WriteFileArray");
        VFSTable.SetFunction<[](FStringView Path, FStringView Data)
        {
            return VFS::WriteFile(Path, Data);
        }>("WriteFileString");

        FRef FileHelperTable = GlobalsRef.NewTable("FileHelper");
        FileHelperTable.SetFunction<&FileHelper::CreateNewFile>("CreateNewFile");

        FRef PathTable = GlobalsRef.NewTable("Paths");
        PathTable.SetFunction<&Paths::Exists>("Exists");
        PathTable.SetFunction<&Paths::GetEngineContentDirectory>("GetEngineContentDirectory");
        PathTable.SetFunction<&Paths::GetEngineConfigDirectory>("GetEngineConfigDirectory");
        PathTable.SetFunction<&Paths::GetEngineShadersDirectory>("GetEngineShadersDirectory");
    }

    static void RegisterRenderLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef RHITable = GlobalsRef.NewTable("RHI");
        RHITable.SetFunction<&IRenderContext::WaitIdle>("WaitIdle", GRenderContext);
        RHITable.SetFunction<&IRenderContext::GetAllocatedMemory>("GetAllocatedMemory", GRenderContext);
        RHITable.SetFunction<&IRenderContext::GetAvailableMemory>("GetAvailableMemory", GRenderContext);
        RHITable.SetFunction<&IRenderContext::SetVSyncEnabled>("SetVSyncEnabled");
        RHITable.SetFunction<&IRenderContext::IsVSyncEnabled>("IsVSyncEnabled");
    }

    static void RegisterECSLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef ECSTable = GlobalsRef.NewTable("ECS");
        ECSTable.SetFunction<&ECS::Utils::IsParent>("IsEntityParent");
        
        ECSTable.SetFunction<&ECS::Utils::IsEntityValid>("IsEntityValid");
        
        ECSTable.SetFunction<&ECS::Utils::DuplicateEntity>("DuplicateEntity");
        ECSTable.SetFunction<&ECS::Utils::DestroyEntity>("DestroyEntity");
        
        ECSTable.SetFunction<&ECS::Utils::TranslateEntity>("TranslateEntity");
        ECSTable.SetFunction<&ECS::Utils::GetDirectionVector>("GetDirectionVector");

        ECSTable.SetFunction<&ECS::Utils::GetEntityLocation>("GetEntityLocation");
        ECSTable.SetFunction<&ECS::Utils::GetEntityRotation>("GetEntityRotation");
        ECSTable.SetFunction<&ECS::Utils::GetEntityScale>("GetEntityScale");

        ECSTable.SetFunction<&ECS::Utils::SetEntityLocation>("SetEntityLocation");
        ECSTable.SetFunction<&ECS::Utils::SetEntityRotation>("SetEntityRotation");
        ECSTable.SetFunction<&ECS::Utils::SetEntityScale>("SetEntityScale");
    }

    static void RegisterMathLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef MathTable = GlobalsRef.NewTable("Math");

        MathTable.Set("Pi",        Math::Pi<float>());
        MathTable.Set("TwoPi",     Math::TwoPi<float>());
        MathTable.Set("HalfPi",    Math::HalfPi<float>());
        MathTable.Set("Epsilon",   Math::Epsilon<float>());
        MathTable.Set("Infinity",  eastl::numeric_limits<float>::infinity());
        
        MathTable.SetFunction<[](FVector3 Target, FVector3 From) { return Math::FindLookAtRotation(Target, From); }>("FindLookAtRotation");

        MathTable.SetFunction<[](float X) { return Math::Abs(X); }>("Abs");
        MathTable.SetFunction<[](float X) { return Math::Sign(X); }>("Sign");
        MathTable.SetFunction<[](float X) { return Math::Floor(X); }>("Floor");
        MathTable.SetFunction<[](float X) { return Math::Ceil(X); }>("Ceil");
        MathTable.SetFunction<[](float X) { return Math::Round(X); }>("Round");
        MathTable.SetFunction<[](float X) { return Math::Fract(X); }>("Fract");
        MathTable.SetFunction<[](float X) { return Math::Sqrt(X); }>("Sqrt");
        MathTable.SetFunction<[](float X) { return Math::InverseSqrt(X); }>("InverseSqrt");
        MathTable.SetFunction<[](float X, float Y) { return Math::Mod(X, Y); }>("Mod");
        MathTable.SetFunction<[](float X, float Y) { return Math::Pow(X, Y); }>("Pow");
        MathTable.SetFunction<[](float X) { return Math::Exp(X); }>("Exp");
        MathTable.SetFunction<[](float X) { return Math::Log(X); }>("Log");
        MathTable.SetFunction<[](float X) { return Math::Exp2(X); }>("Exp2");
        MathTable.SetFunction<[](float X) { return Math::Log2(X); }>("Log2");
        
        MathTable.SetFunction<[](float A, float B, float T) { return Math::Mix(A, B, T); }>("Lerp");
        MathTable.SetFunction<[](float X, float E0, float E1) { return Math::SmoothStep(E0, E1, X); }>("SmoothStep");
        MathTable.SetFunction<[](float X, float E) { return Math::Step(E, X); }>("Step");
        MathTable.SetFunction<[](float X, float Min, float Max) { return Math::Clamp(X, Min, Max); }>("Clamp");
        MathTable.SetFunction<[](float A, float B) { return Math::Min(A, B); }>("Min");
        MathTable.SetFunction<[](float A, float B) { return Math::Max(A, B); }>("Max");
        
        MathTable.SetFunction<[](float X) { return Math::Radians(X); }>("Radians");
        MathTable.SetFunction<[](float X) { return Math::Degrees(X); }>("Degrees");
        MathTable.SetFunction<[](float X) { return Math::Sin(X); }>("Sin");
        MathTable.SetFunction<[](float X) { return Math::Cos(X); }>("Cos");
        MathTable.SetFunction<[](float X) { return Math::Tan(X); }>("Tan");
        MathTable.SetFunction<[](float X) { return Math::Asin(X); }>("Asin");
        MathTable.SetFunction<[](float X) { return Math::Acos(X); }>("Acos");
        MathTable.SetFunction<[](float Y, float X) { return Math::Atan2(Y, X); }>("Atan2");
        
        MathTable.SetFunction<[](FVector2 A, FVector2 B) { return Math::Dot(A, B); }>("Dot2");
        MathTable.SetFunction<[](FVector2 V) { return Math::Length(V); }>("Length2");
        MathTable.SetFunction<[](FVector2 V) { return Math::Normalize(V); }>("Normalize2");
        MathTable.SetFunction<[](FVector2 A, FVector2 B) { return Math::Distance(A, B); }>("Distance2");
        MathTable.SetFunction<[](FVector2 A, FVector2 B, float T) { return Math::Mix(A, B, T); }>("Lerp2");
        MathTable.SetFunction<[](FVector2 A, FVector2 B) { return Math::Reflect(A, B); }>("Reflect2");
        
        MathTable.SetFunction<[](FVector3 A, FVector3 B) { return Math::Dot(A, B); }>("Dot3");
        MathTable.SetFunction<[](FVector3 A, FVector3 B) { return Math::Cross(A, B); }>("Cross");
        MathTable.SetFunction<[](FVector3 V) { return Math::Length(V); }>("Length3");
        MathTable.SetFunction<[](FVector3 V) { return Math::Normalize(V); }>("Normalize3");
        MathTable.SetFunction<[](FVector3 A, FVector3 B) { return Math::Distance(A, B); }>("Distance3");
        MathTable.SetFunction<[](FVector3 A, FVector3 B, float T) { return Math::Mix(A, B, T); }>("Lerp3");
        MathTable.SetFunction<[](FVector3 A, FVector3 B) { return Math::Reflect(A, B); }>("Reflect3");
        MathTable.SetFunction<[](FVector3 A, FVector3 B, float Eta) { return Math::Refract(A, B, Eta); }>("Refract3");
        
        MathTable.SetFunction<[](FVector4 A, FVector4 B) { return Math::Dot(A, B); }>("Dot4");
        MathTable.SetFunction<[](FVector4 V) { return Math::Length(V); }>("Length4");
        MathTable.SetFunction<[](FVector4 V) { return Math::Normalize(V); }>("Normalize4");
        MathTable.SetFunction<[](FVector4 A, FVector4 B, float T) { return Math::Mix(A, B, T); }>("Lerp4");
        
        MathTable.SetFunction<[](FQuat A, FQuat B) { return Math::Dot(A, B); }>("QuatDot");
        MathTable.SetFunction<[](FQuat A, FQuat B, float T) { return Math::Slerp(A, B, T); }>("QuatSlerp");
        MathTable.SetFunction<[](FQuat Q) { return Math::Normalize(Q); }>("QuatNormalize");
        MathTable.SetFunction<[](FQuat Q) { return Math::Inverse(Q); }>("QuatInverse");
        MathTable.SetFunction<[](FQuat Q) { return Math::Conjugate(Q); }>("QuatConjugate");
        MathTable.SetFunction<[](FQuat Q) { return Math::EulerAngles(Q); }>("QuatEuler");
        MathTable.SetFunction<[](float Angle, FVector3 Axis) { return Math::AngleAxis(Angle, Axis); }>("QuatAngleAxis");
        MathTable.SetFunction<[](FVector3 Euler) { return FQuat(Euler); }>("QuatFromEuler");
        MathTable.SetFunction<[](FQuat Q) { return Math::ToMatrix4(Q); }>("QuatToMatrix");
        MathTable.SetFunction<[](FVector3 From, FVector3 To) { return Math::RotationBetween(From, To); }>("QuatFromTo");
        MathTable.SetFunction<[](FQuat Rot) { return Math::Normalize(Math::Rotate(Rot, FVector3(0.0f, 0.0f, 1.0f))); } >("QuatForward");

        GlobalsRef.NewClass<FMatrix4>("Mat4")
            .Register();

        GlobalsRef.NewClass<FQuat>("Quat")
            .AddProperty<&FQuat::x>("X")
            .AddProperty<&FQuat::y>("Y")
            .AddProperty<&FQuat::z>("Z")
            .AddProperty<&FQuat::w>("W")

            .AddFunction<[](FQuat& A, FQuat B) { return A * B; }>(EMetaMethod::Mul)
            .AddFunction<[](FQuat& A, FQuat B) { return A + B; }>(EMetaMethod::Add)
            .AddFunction<[](FQuat& A, FQuat B) { return A - B; }>(EMetaMethod::Sub)
            .AddFunction<[](FQuat& A) { return -A; }>(EMetaMethod::UnaryMinus)
            .AddFunction<[](FQuat& A, FQuat B) { return A == B; }>(EMetaMethod::Eq)
            .AddFunction<[](FQuat& Self) { return Math::ToString(Self); }>(EMetaMethod::ToString)
            .AddFunction<[](FQuat& Self) { return Math::Length(Self); }>(EMetaMethod::Len)

            .AddFunction<[](FQuat& Self) { return Math::EulerAngles(Self); }>("EulerAngles")
            .AddFunction<[](FQuat& Self) { return Math::Normalize(Self); }>("Normalize")
            .AddFunction<[](FQuat& Self) { return Math::Inverse(Self); }>("Inverse")
            .AddFunction<[](FQuat& Self) { return Math::Length(Self); }>("Length")
            .AddFunction<[](FQuat& Self) { return Math::Conjugate(Self); }>("Conjugate")
            .AddFunction<[](FQuat& Self) { return Math::Dot(Self, Self); }>("LengthSquared")
            .AddFunction<[](FQuat& A, FQuat B) { return Math::Dot(A, B); }>("Dot")
            .AddFunction<[](FQuat& A, FQuat B, float T) { return Math::Slerp(A, B, T); }>("SLerp")
            .AddFunction<[](FQuat& A, FQuat B, float T) { return Math::Lerp(A, B, T); }>("Lerp")
            .AddFunction<[](FQuat& Self) { return Math::ToMatrix4(Self); }>("ToMatrix")
            .AddFunction<[](float Angle, FVector3 Axis) { return Math::AngleAxis(Angle, Axis); }>("FromAngleAxis")
            .AddFunction<[](FQuat& Self, FVector3 V) { return Self * V; }>("RotateVector")
            .Register();
    }

    static void RegisterInputLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef InputTable = GlobalsRef.NewTable("Input");
        InputTable.SetFunction<&FInputProcessor::IsKeyDown>("IsKeyDown", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsKeyUp>("IsKeyUp", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsKeyPressed>("IsKeyPressed", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsKeyRepeated>("IsKeyRepeated", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsKeyReleased>("IsKeyReleased", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsMouseButtonPressed>("IsMouseButtonPressed", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsMouseButtonReleased>("IsMouseButtonReleased", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::GetMouseButtonHeldTime>("GetMouseButtonHeldTime", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsMouseButtonDown>("IsMouseButtonDown", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::IsMouseButtonUp>("IsMouseButtonUp", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::GetMouseX>("GetMouseX", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::GetMouseY>("GetMouseY", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::GetMouseZ>("GetMouseZ", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::GetMouseDeltaX>("GetMouseDeltaX", &FInputProcessor::Get());
        InputTable.SetFunction<&FInputProcessor::GetMouseDeltaY>("GetMouseDeltaY", &FInputProcessor::Get());
        // Accept strings: "Hidden", "Normal", "Captured".
        InputTable.SetFunction<[](FStringView Mode)
        {
            if      (Mode == "Hidden")
            {
                FInputProcessor::Get().SetMouseMode(EMouseMode::Hidden);
            }
            else if (Mode == "Normal")
            {
                FInputProcessor::Get().SetMouseMode(EMouseMode::Normal);
            }
            else if (Mode == "Captured")
            {
                FInputProcessor::Get().SetMouseMode(EMouseMode::Captured);
            }
            else
            {
                LOG_WARN("[Input] SetMouseMode: unknown mode '{}'. Use 'Hidden', 'Normal', or 'Captured'.",
                         FString(Mode.data(), Mode.size()).c_str());
            }
        }>("SetMouseMode");


        InputTable.SetFunction<[](FStringView Mode)
        {
            EInputMode Out = EInputMode::Game;
            if      (Mode == "Game")
            {
                Out = EInputMode::Game;
            }
            else if (Mode == "UI")
            {
                Out = EInputMode::UI;
            }
            else if (Mode == "GameAndUI" || Mode == "Both")
            {
                Out = EInputMode::GameAndUI;
            }
            else
            {
                LOG_WARN("[Input] SetMode: unknown mode '{}'. Use 'Game', 'UI', or 'GameAndUI'.",
                         FString(Mode.data(), Mode.size()).c_str());
                return;
            }
            FInputProcessor::Get().SetInputMode(Out);
            LOG_INFO("[Input] Mode -> {}", InputModeToString(Out));
        }>("SetMode");
        InputTable.SetFunction<[]() -> FString
        {
            return FString(InputModeToString(FInputProcessor::Get().GetInputMode()));
        }>("GetMode");

        InputTable.SetFunction<[](FStringView Name) -> bool
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return false;
            return FInputActionMap::Get().IsActionDown(FName(FString(Name.data(), Name.size()).c_str()), V->GetContext());
        }>("IsActionDown");

        InputTable.SetFunction<[](FStringView Name) -> bool
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return false;
            return FInputActionMap::Get().IsActionPressed(FName(FString(Name.data(), Name.size()).c_str()), V->GetContext());
        }>("IsActionPressed");

        InputTable.SetFunction<[](FStringView Name) -> bool
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return false;
            return FInputActionMap::Get().IsActionReleased(FName(FString(Name.data(), Name.size()).c_str()), V->GetContext());
        }>("IsActionReleased");

        InputTable.SetFunction<[](FStringView Name) -> float
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return 0.0f;
            return FInputActionMap::Get().GetActionAxis(FName(FString(Name.data(), Name.size()).c_str()), V->GetContext());
        }>("GetActionAxis");

        // Returns an ID for UnbindAction; scoped to the active context.
        InputTable.SetFunction<[](FStringView Name, Lua::FRef Callback) -> uint64
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return uint64{0};
            return V->GetContext().RegisterActionCallback(
                FName(FString(Name.data(), Name.size()).c_str()),
                FInputContext::EActionTrigger::Pressed,
                std::move(Callback));
        }>("OnActionPressed");

        InputTable.SetFunction<[](FStringView Name, Lua::FRef Callback) -> uint64
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return uint64{0};
            return V->GetContext().RegisterActionCallback(
                FName(FString(Name.data(), Name.size()).c_str()),
                FInputContext::EActionTrigger::Released,
                std::move(Callback));
        }>("OnActionReleased");

        InputTable.SetFunction<[](uint64 Id)
        {
            FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return;
            V->GetContext().UnregisterActionCallback(Id);
        }>("UnbindAction");
    }

    static void RegisterAudioLibrary(lua_State* L, FRef& GlobalsRef)
    {
        FRef AudioTable = GlobalsRef.NewTable("Audio");
        AudioTable.SetFunction<[](FStringView File, FVector3 Location) { return GAudioContext->PlaySoundAtLocation(File, Location); }>("PlaySoundAtLocation");
        AudioTable.SetFunction<[](FStringView File) { return GAudioContext->PlaySound2D(File); }>("PlaySound2D");
        AudioTable.SetFunction<[](FStringView File, FVector3 Loc, float Volume, float Pitch, bool bLooping)
            { return GAudioContext->PlaySoundAtLocation(File, Loc, Volume, Pitch, 1.0f, 50.0f, bLooping); }>("PlaySoundAtLocationEx");
        AudioTable.SetFunction<[](FStringView File, float Volume, float Pitch, bool bLooping)
            { return GAudioContext->PlaySound2D(File, Volume, Pitch, bLooping); }>("PlaySound2DEx");
        AudioTable.SetFunction<[]() { GAudioContext->StopAllSounds(); }>("StopAllSounds");

        GlobalsRef.NewClass<FAudioHandle>("AudioHandle")
            .AddFunction<&FAudioHandle::IsValid>("IsValid")
            .AddFunction<[](FAudioHandle& Self) { GAudioContext->StopSound(Self, EAudioStopMode::Immediate); }>("Stop")
            .AddFunction<[](FAudioHandle& Self) { GAudioContext->StopSound(Self, EAudioStopMode::AllowFadeOut); }>("FadeOut")
            .AddFunction<[](FAudioHandle& Self, float Volume) { GAudioContext->SetVolume(Self, Volume); }>("SetVolume")
            .AddFunction<[](FAudioHandle& Self, float Pitch)  { GAudioContext->SetPitch(Self, Pitch); }>("SetPitch")
            .AddFunction<[](FAudioHandle& Self, bool bLoop)   { GAudioContext->SetLooping(Self, bLoop); }>("SetLooping")
            .AddFunction<[](FAudioHandle& Self, FVector3 P)  { GAudioContext->SetPosition(Self, P); }>("SetPosition")
            .AddFunction<[](FAudioHandle& Self, float Min, float Max) { GAudioContext->SetMinMaxDistance(Self, Min, Max); }>("SetMinMaxDistance")
            .Register();
    }

    void FScriptingContext::Initialize()
    {
        L = lua_newstate(ScriptingMemoryReallocFn, this);

        lua_Callbacks* Callbacks    = lua_callbacks(L);
        Callbacks->useratom         = AtomString;
        Callbacks->panic            = LuaPanicHandler;

        // Hook before any scripts load so callbacks are visible to every coroutine.
        FLuaDebugger::Get().Initialize(L);

        luaL_openlibs(L);

        lua_pushcfunction(L, LuminaLuaPrint, "LuminaLuaPrint");
        lua_setglobal(L, "print");

        // VFS-backed resolver; Luau ships no package.searchers machinery.
        lua_pushcfunction(L, LuminaLuaRequire, "require");
        lua_setglobal(L, "require");

        lua_pushvalue(L, LUA_GLOBALSINDEX);
        FRef GlobalsRef(L, -1);

        CWorld::RegisterLuaModule(GlobalsRef);
        RmlUi::RegisterLuaModule(GlobalsRef);
        Nav::RegisterLuaModule(GlobalsRef);

        RegisterConsoleLibrary(L, GlobalsRef);
        RegisterTimeLibrary(L, GlobalsRef);
        RegisterEngineLibrary(L, GlobalsRef);
        RegisterFileSystemLibrary(L, GlobalsRef);
        RegisterRenderLibrary(L, GlobalsRef);
        RegisterECSLibrary(L, GlobalsRef);
        RegisterMathLibrary(L, GlobalsRef);
        RegisterInputLibrary(L, GlobalsRef);
        RegisterAudioLibrary(L, GlobalsRef);

        RegisterRuntimeComponentMetatable(L);
        RegisterAllRuntimeComponentTypeGlobals(L);

        // Re-register on registry updates (discovery finishes after init; deferred to avoid
        // deadlock with AssetsMutex held during the broadcast).
        FAssetRegistry::Get().GetOnAssetRegistryUpdated().AddLambda([this]
        {
            MainThread::Enqueue([this]
            {
                RegisterAllRuntimeComponentTypeGlobals(L);
            });
        });

        LoadStdlibFiles();
    }

    void FScriptingContext::LoadStdlibFiles()
    {
        static const char* const kStdlibModules[] =
        {
            "Stdlib/EntityScript",
            "Stdlib/Random",
            "Stdlib/Color",
            "Stdlib/Tween",
        };

        for (const char* ModName : kStdlibModules)
        {
            if (!RequireModule(L, FStringView(ModName)))
            {
                LOG_ERROR("Lua stdlib: failed to preload module '{}'", ModName);
                continue;
            }

            FStringView Path(ModName);
            const size_t LastSlash = Path.find_last_of('/');
            const FStringView Basename = (LastSlash != FStringView::npos)
                ? Path.substr(LastSlash + 1)
                : Path;

            lua_setglobal(L, FString(Basename).c_str());
        }
    }

    void FScriptingContext::ReloadStdlib()
    {
        FWriteScopeLock Lock(SharedMutex);

        TVector<FName> Paths;
        Paths.reserve(ModuleCache.size());
        for (const auto& Pair : ModuleCache)
        {
            Paths.push_back(Pair.first);
        }
        for (const FName& Path : Paths)
        {
            ReloadModule(FStringView(Path.c_str()));
        }

        LOG_INFO("Lua stdlib: reloaded {} module(s)", Paths.size());
    }

    void FScriptingContext::SandboxGlobals()
    {
        luaL_sandbox(L);
    }

    void FScriptingContext::Shutdown()
    {
        FLuaDebugger::Get().Shutdown();
        lua_close(L);
        L = nullptr;
    }

    void FScriptingContext::ProcessDeferredActions()
    {
        FWriteScopeLock Lock(SharedMutex);

        DeferredActions.ProcessAllOf<FScriptDelete>([&](const FScriptDelete& Delete)
        {
            OnScriptDeleted.Broadcast(Delete.Path);
        });

        DeferredActions.ProcessAllOf<FScriptRename>([&](const FScriptRename& Reload)
        {

        });

        // Coalesce duplicate reload requests within one tick. The editor's OnSave fires a
        // direct reload, the directory watcher fires its own once disk catches up, and Windows'
        // ReadDirectoryChangesW often emits multiple FILE_ACTION_MODIFIED events per save.
        THashSet<FName> SeenPaths;
        DeferredActions.ProcessAllOf<FScriptLoad>([&](const FScriptLoad& Load)
        {
            if (!SeenPaths.insert(FName(Load.Path)).second)
            {
                return;
            }
            OnScriptLoaded.Broadcast(Load.Path);
            ReloadScripts(Load.Path);
        });
    }
    
    int FScriptingContext::GetScriptMemoryUsageBytes() const
    {
        if (L == nullptr)
        {
            return 0;
        }

        int KB        = lua_gc(L, LUA_GCCOUNT, 0);
        int Remainder = lua_gc(L, LUA_GCCOUNTB, 0);
        return (KB * 1024) + Remainder;
    }

    void FScriptingContext::ScriptReloaded(FStringView ScriptPath)
    {
        FWriteScopeLock Lock(SharedMutex);

        // Drop the cache entry so the next LoadUniqueScriptPath rereads from
        ScriptCache.erase(FName(ScriptPath));

        // If the saved file is also a require()'d module, refresh its cached value
        // identity-preservingly so live scripts pick up the new functions.
        if (ModuleCache.find(FName(ScriptPath)) != ModuleCache.end())
        {
            ReloadModule(ScriptPath);
        }

        DeferredActions.EnqueueAction<FScriptLoad>(ScriptPath);
    }

    void FScriptingContext::ScriptCreated(FStringView ScriptPath)
    {
        FWriteScopeLock Lock(SharedMutex);
    }

    void FScriptingContext::ScriptRenamed(FStringView NewPath, FStringView OldPath)
    {
        FWriteScopeLock Lock(SharedMutex);

        ScriptCache.erase(FName(OldPath));

        DeferredActions.EnqueueAction<FScriptRename>(NewPath, OldPath);
    }

    void FScriptingContext::ScriptDeleted(FStringView ScriptPath)
    {
        FWriteScopeLock Lock(SharedMutex);

        ScriptCache.erase(FName(ScriptPath));

        DeferredActions.EnqueueAction<FScriptDelete>(ScriptPath);
    }

    namespace
    {
        bool CompileSourceToBytecode(FStringView Code, TVector<uint8>& Out, FCompileDiagnostic* OutDiag)
        {
            lua_CompileOptions Options{};
            // Editor: opt 1 + full debug so the debugger can attach breakpoints; opt 2 inlines past lua_breakpoint.
            #if USING(WITH_EDITOR)
                Options.debugLevel = 2;
                Options.optimizationLevel = 1;
            #else
                Options.debugLevel = 0;
                Options.optimizationLevel = 2;
            #endif

            size_t BytecodeSize = 0;
            char* Bytecode = luau_compile(Code.data(), Code.length(), &Options, &BytecodeSize);
            if (Bytecode == nullptr)
            {
                if (OutDiag)
                {
                    OutDiag->Message = "luau_compile returned null";
                    OutDiag->Line    = -1;
                }
                Out.clear();
                return false;
            }

            // Luau "version 0" bytecode = compile error; see BytecodeBuilder::getError.
            if (BytecodeSize > 0 && Bytecode[0] == 0)
            {
                if (OutDiag)
                {
                    const char* MsgStart = Bytecode + 1;
                    const size_t MsgLen  = BytecodeSize - 1;
                    OutDiag->Message.assign(MsgStart, MsgLen);

                    OutDiag->Line = -1;
                    if (MsgLen > 1 && MsgStart[0] == ':')
                    {
                        int Parsed = 0, Consumed = 0;
                        if (sscanf(MsgStart + 1, "%d:%n", &Parsed, &Consumed) == 1)
                        {
                            OutDiag->Line = Parsed;
                        }
                    }
                }
                Out.clear();
                free(Bytecode);
                return false;
            }

            Out.assign(reinterpret_cast<uint8*>(Bytecode), reinterpret_cast<uint8*>(Bytecode) + BytecodeSize);
            free(Bytecode);
            return true;
        }
    }

    TSharedPtr<FScript> FScriptingContext::LoadUniqueScriptPath(FStringView Path)
    {
        LUMINA_PROFILE_SCOPE();

        FName PathName(Path);

        // Cold path: read, compile, build schema, store. Subsequent loads use the cache below.
        auto CacheIt = ScriptCache.find(PathName);
        if (CacheIt == ScriptCache.end())
        {
            FString ScriptData;
            if (!VFS::ReadFile(ScriptData, Path))
            {
                LOG_ERROR("Lua - Failed to read script file: {}", Path);
                return {};
            }

            if (ScriptData.empty())
            {
                LOG_WARN("Lua - Script file is empty: {}", Path);
                return {};
            }

            FScriptCacheEntry Entry;
            FCompileDiagnostic Diag;
            if (!CompileSourceToBytecode(ScriptData, Entry.Bytecode, &Diag))
            {
                Diag.Path.assign(Path.data(), Path.size());
                LOG_ERROR("Lua - Compile failed for {}: {}", Path, Diag.Message);
                OnScriptCompileError.Broadcast(Path, Diag);
                return {};
            }
            OnScriptCompileSuccess.Broadcast(Path);

            // Piggyback schema harvest off the first instance.
            TSharedPtr<FScript> FirstScript = InstantiateFromBytecode(Entry.Bytecode, Path,
                                                                      &Entry.ExportsSchema,
                                                                      &Entry.ExportDefaults);
            if (FirstScript == nullptr)
            {
                return {};
            }

            FirstScript->Path = Path;
            RegisteredScripts[PathName].emplace_back(FirstScript);
            ScriptCache.emplace(PathName, eastl::move(Entry));
            FLuaDebugger::Get().OnScriptLoaded(*FirstScript);
            return FirstScript;
        }

        const FScriptCacheEntry& CachedEntry = CacheIt->second;
        TSharedPtr<FScript> Script = InstantiateFromBytecode(CachedEntry.Bytecode, Path, nullptr, nullptr);
        if (Script == nullptr)
        {
            return {};
        }

        Script->Path           = Path;
        Script->ExportsSchema  = CachedEntry.ExportsSchema;
        Script->ExportDefaults = CachedEntry.ExportDefaults;

        RegisteredScripts[PathName].emplace_back(Script);
        FLuaDebugger::Get().OnScriptLoaded(*Script);
        return Script;
    }

    TSharedPtr<FScript> FScriptingContext::InstantiateFromBytecode(
        const TVector<uint8>& Bytecode,
        FStringView Name,
        FScriptExportSchema* OutSchema,
        TVector<FScriptPropertyEntry>* OutDefaults) const
    {
        LUMINA_PROFILE_SCOPE();

        int StackBefore = lua_gettop(L);
        lua_State* Thread = lua_newthread(L);
        FRef ThreadRef(L, -1);

        luaL_sandboxthread(Thread);

        // Virtual path as chunkname so lua_Debug::source matches editor/registry keys.
        int LoadResult = luau_load(Thread, Name.data(),
                                   reinterpret_cast<const char*>(Bytecode.data()),
                                   Bytecode.size(), 0);
        if (LoadResult != LUA_OK)
        {
            LOG_ERROR("Lua - Failed to load: {}", lua_tostring(Thread, -1));
            return {};
        }

        // Pin closure pre-pcall; debugger needs it for lua_breakpoint or breakpoints silently no-op.
        lua_pushvalue(Thread, -1);
        FRef MainFunctionRef(Thread, -1);

        int CallResult = lua_pcall(Thread, 0, 1, 0);
        if (CallResult != LUA_OK)
        {
            LOG_ERROR("Runtime Error {}", lua_tostring(Thread, -1));
            return {};
        }

        // First-load schema harvest from the module's `Exports` table.
        if (OutSchema != nullptr || OutDefaults != nullptr)
        {
            if (lua_istable(Thread, -1))
            {
                lua_getfield(Thread, -1, "Exports");
                if (lua_istable(Thread, -1))
                {
                    FScriptExportSchema TempSchema;
                    TVector<FScriptPropertyEntry> TempDefaults;
                    BuildSchemaFromExportsTable(Thread, -1, TempSchema, TempDefaults);
                    if (OutSchema)   *OutSchema   = TempSchema;
                    if (OutDefaults) *OutDefaults = eastl::move(TempDefaults);
                }
                lua_pop(Thread, 1);
            }
        }

        auto NewScript = MakeShared<FScript>();
        NewScript->Name             = Name;
        NewScript->Path             = "";
        NewScript->Reference        = FRef(Thread, -1);
        NewScript->MainFunction     = eastl::move(MainFunctionRef);

        if (OutSchema)   NewScript->ExportsSchema  = *OutSchema;
        if (OutDefaults) NewScript->ExportDefaults = *OutDefaults;

        lua_pushvalue(Thread, LUA_GLOBALSINDEX);
        NewScript->Environment  = FRef(Thread, -1);
        NewScript->Thread       = ThreadRef;

        int StackAfter = lua_gettop(L);
        DEBUG_ASSERT(StackAfter == StackBefore);

        return NewScript;
    }

    TSharedPtr<FScript> FScriptingContext::LoadUniqueScript(FStringView Code, FStringView Name) const
    {
        LUMINA_PROFILE_SCOPE();

        // Raw-source path (e.g. project boot script); no cache, same instance pipeline.
        TVector<uint8> Bytecode;
        if (!CompileSourceToBytecode(Code, Bytecode))
        {
            return {};
        }

        FScriptExportSchema Schema;
        TVector<FScriptPropertyEntry> Defaults;
        return InstantiateFromBytecode(Bytecode, Name, &Schema, &Defaults);
    }

    void FScriptingContext::InvalidateScriptCache(FStringView Path)
    {
        ScriptCache.erase(FName(Path));
    }

    bool FScriptingContext::ResolveModulePath(FStringView ModuleName, FString& OutPath) const
    {
        // Absolute VFS path: pass through, append `.luau` if the user omitted it.
        if (!ModuleName.empty() && ModuleName[0] == '/')
        {
            OutPath.assign(ModuleName.data(), ModuleName.length());
            if (OutPath.size() < 5 || OutPath.compare(OutPath.size() - 5, 5, ".luau") != 0)
            {
                OutPath += ".luau";
            }
            return VFS::Exists(OutPath);
        }

        // `Foo.Bar` → `Foo/Bar`. Common Luau convention so `require("Stdlib.EntityScript")`
        // and `require("Stdlib/EntityScript")` both work.
        FString Normalized(ModuleName.data(), ModuleName.length());
        for (char& C : Normalized)
        {
            if (C == '.') C = '/';
        }

        // Search roots, in order. Engine stdlib first so user code can override only by
        // explicit absolute path, never by accident.
        static const char* const kRoots[] =
        {
            "/Engine/Resources/Content/Scripts/",
            "/Game/Scripts/",
        };

        for (const char* Root : kRoots)
        {
            FString Candidate = Root;
            Candidate += Normalized;
            Candidate += ".luau";
            if (VFS::Exists(Candidate))
            {
                OutPath = eastl::move(Candidate);
                return true;
            }
        }

        return false;
    }

    bool FScriptingContext::LoadModuleOntoThread(lua_State* RequestingThread, const FString& ResolvedPath, int ExistingTableRegistryRef)
    {
        const FName CacheKey(FStringView(ResolvedPath.c_str()));
        auto It = ModuleCache.find(CacheKey);

        TVector<uint8>* Bytecode = nullptr;
        if (It != ModuleCache.end() && !It->second.Bytecode.empty())
        {
            Bytecode = &It->second.Bytecode;
        }
        else
        {
            FString Source;
            if (!VFS::ReadFile(Source, ResolvedPath))
            {
                LOG_ERROR("Lua require: failed to read '{}'", ResolvedPath);
                return false;
            }

            FModuleCacheEntry NewEntry;
            FCompileDiagnostic Diag;
            if (!CompileSourceToBytecode(Source, NewEntry.Bytecode, &Diag))
            {
                Diag.Path = ResolvedPath;
                LOG_ERROR("Lua require: compile failed for '{}': {}", ResolvedPath, Diag.Message);
                OnScriptCompileError.Broadcast(FStringView(ResolvedPath.c_str(), ResolvedPath.size()), Diag);
                return false;
            }
            OnScriptCompileSuccess.Broadcast(FStringView(ResolvedPath.c_str(), ResolvedPath.size()));

            auto Inserted = ModuleCache.emplace(CacheKey, eastl::move(NewEntry));
            It = Inserted.first;
            Bytecode = &It->second.Bytecode;
        }
        
        const int LoadResult = luau_load(L, ResolvedPath.c_str(),
                                         reinterpret_cast<const char*>(Bytecode->data()),
                                         Bytecode->size(), 0);
        if (LoadResult != LUA_OK)
        {
            LOG_ERROR("Lua require: load failed for '{}': {}", ResolvedPath, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }

        const int CallResult = lua_pcall(L, 0, 1, 0);
        if (CallResult != LUA_OK)
        {
            LOG_ERROR("Lua require: runtime error in '{}': {}", ResolvedPath, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }

        // Modules that return nothing get cached as `true`, matching Lua convention.
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }

        // Identity-preserving reload: copy new keys into the existing table and discard
        // the freshly-built one. Only valid when both sides are tables.
        if (ExistingTableRegistryRef != LUA_NOREF && lua_istable(L, -1))
        {
            lua_getref(L, ExistingTableRegistryRef);
            if (lua_istable(L, -1))
            {
                // Stack: [NewTable, OldTable]. Iterate NewTable, rawset into OldTable.
                lua_pushnil(L);
                while (lua_next(L, -3) != 0)
                {
                    lua_pushvalue(L, -2); // dup key
                    lua_pushvalue(L, -2); // dup value
                    lua_rawset(L, -5);    // OldTable[key] = value
                    lua_pop(L, 1);        // pop value, keep key for next iteration
                }
                // Drop NewTable; OldTable becomes the result.
                lua_remove(L, -2);
                It->second.RegistryRef = ExistingTableRegistryRef;
            }
            else
            {
                lua_pop(L, 1); // not a table, fall through to replace
                if (It->second.RegistryRef != LUA_NOREF)
                {
                    lua_unref(L, It->second.RegistryRef);
                }
                lua_pushvalue(L, -1);
                It->second.RegistryRef = lua_ref(L, -1);
                lua_pop(L, 1);
            }
        }
        else
        {
            // First load (or non-table replace): drop any prior pin and re-ref.
            if (It->second.RegistryRef != LUA_NOREF)
            {
                lua_unref(L, It->second.RegistryRef);
            }
            lua_pushvalue(L, -1);
            It->second.RegistryRef = lua_ref(L, -1);
            lua_pop(L, 1);
        }

        // Result is on top of L. If the requester is a different thread, hop it over via
        // the registry ref we just pinned (avoids lua_xmove and keeps stacks balanced).
        if (RequestingThread != L)
        {
            lua_pop(L, 1); // discard from main state
            lua_getref(RequestingThread, It->second.RegistryRef);
        }
        return true;
    }

    bool FScriptingContext::RequireModule(lua_State* Thread, FStringView ModuleName)
    {
        FString ResolvedPath;
        if (!ResolveModulePath(ModuleName, ResolvedPath))
        {
            LOG_ERROR("Lua require: module '{}' not found in any search root", ModuleName);
            return false;
        }

        const FName CacheKey(FStringView(ResolvedPath.c_str()));
        auto It = ModuleCache.find(CacheKey);
        if (It != ModuleCache.end() && It->second.RegistryRef != LUA_NOREF)
        {
            // Cache hit
            lua_getref(Thread, It->second.RegistryRef);
            return true;
        }

        return LoadModuleOntoThread(Thread, ResolvedPath, LUA_NOREF);
    }

    void FScriptingContext::ReloadModule(FStringView ModuleName)
    {
        FString ResolvedPath;
        if (!ResolveModulePath(ModuleName, ResolvedPath))
        {
            LOG_WARN("Lua require: reload skipped, module '{}' not found", ModuleName);
            return;
        }

        const FName CacheKey(FStringView(ResolvedPath.c_str()));
        auto It = ModuleCache.find(CacheKey);

        // Wipe bytecode so the next load re-reads the file from disk.
        int ExistingRef = LUA_NOREF;
        if (It != ModuleCache.end())
        {
            It->second.Bytecode.clear();
            ExistingRef = It->second.RegistryRef;
        }

        // Run on the main state, module bodies belong to the VM, not any one script thread.
        if (!LoadModuleOntoThread(L, ResolvedPath, ExistingRef))
        {
            return;
        }
        lua_pop(L, 1); // discard returned value; pin lives in the cache via ref
    }

    TVector<TSharedPtr<FScript>> FScriptingContext::GetAllRegisteredScripts()
    {
        TVector<TSharedPtr<FScript>> ReturnValue;

        for (auto& [Path, Vector] : RegisteredScripts)
        {
            for (TWeakPtr<FScript>& WeakPtr : Vector)
            {
                if (auto StrongPtr = WeakPtr.lock())
                {
                    ReturnValue.emplace_back(Move(StrongPtr));
                }
            }
        }
        
        return ReturnValue;
    }

    void FScriptingContext::RunGC()
    {
        if (L == nullptr)
        {
            return;
        }

        // Userdata finalizers release the C++ strong refs they hold; those releases can unpin
        // further Lua objects (registry FRefs in CObject dtors), so collect until the heap is
        // stable to drain cascading ownership chains instead of leaking them past teardown.
        auto TotalBytes = [&] { return (lua_gc(L, LUA_GCCOUNT, 0) * 1024) + lua_gc(L, LUA_GCCOUNTB, 0); };

        int Prev = TotalBytes();
        for (int Pass = 0; Pass < 8; ++Pass)
        {
            lua_gc(L, LUA_GCCOLLECT, 0);
            const int Now = TotalBytes();
            if (Now >= Prev)
            {
                break;
            }
            Prev = Now;
        }
    }

    FRef FScriptingContext::GetGlobalsRef() const
    {
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        return FRef(L, -1);
    }

#if LUAI_GCMETRICS
    const GCMetrics* FScriptingContext::GetGCMetrics() const
    {
        return &L->global->gcmetrics;
    }
    #endif

    void FScriptingContext::ReloadScripts(FStringView Path)
    {
        LOG_INFO("Reloaded Scripts: {}", Path);
        
        FString Source;
        if (!VFS::ReadFile(Source, Path))
        {
            return;
        }

        TVector<uint8> Bytecode;
        FCompileDiagnostic Diag;
        if (CompileSourceToBytecode(Source, Bytecode, &Diag))
        {
            OnScriptCompileSuccess.Broadcast(Path);
        }
        else
        {
            Diag.Path.assign(Path.data(), Path.size());
            OnScriptCompileError.Broadcast(Path, Diag);
        }
    }

    namespace
    {
        ELuaSymbolKind ClassifyTop(lua_State* L)
        {
            switch (lua_type(L, -1))
            {
                case LUA_TFUNCTION:    return ELuaSymbolKind::Function;
                case LUA_TTABLE:       return ELuaSymbolKind::Table;
                default:               return ELuaSymbolKind::Value;
            }
        }

        // Fills function-shape fields via lua_getinfo "a"; C functions report 0 params + vararg.
        void HarvestFunctionInfoTop(lua_State* L, FLuaSymbol& Out)
        {
            lua_Debug ar = {};
            if (lua_getinfo(L, -1, "a", &ar) == 0)
            {
                Out.bIsVararg = true;
                return;
            }
            Out.ParamCount = static_cast<uint8>(ar.nparams);
            Out.bIsVararg  = ar.isvararg != 0;

            // C functions always report nparams=0, isvararg=1.
            Out.bIsCFunction = (Out.ParamCount == 0 && Out.bIsVararg);
        }

        // Avoids __tostring to keep the harvest side-effect free.
        FString PreviewTopValue(lua_State* L)
        {
            switch (lua_type(L, -1))
            {
                case LUA_TNIL:     return "nil";
                case LUA_TBOOLEAN: return lua_toboolean(L, -1) ? "true" : "false";
                case LUA_TNUMBER:
                {
                    char Buf[64];
                    std::snprintf(Buf, sizeof(Buf), "%.14g", lua_tonumber(L, -1));
                    return FString(Buf);
                }
                case LUA_TSTRING:
                {
                    size_t Len = 0;
                    const char* Str = lua_tolstring(L, -1, &Len);
                    FString Out;
                    Out.reserve(Len + 2);
                    Out.assign("\"");
                    Out.append(Str ? Str : "", Len);
                    Out.append("\"");
                    return Out;
                }
                case LUA_TVECTOR:
                {
                    const float* V = lua_tovector(L, -1);
                    if (!V) return "vector(?)";
                    char Buf[96];
                    std::snprintf(Buf, sizeof(Buf), "vec3(%.4g, %.4g, %.4g)", V[0], V[1], V[2]);
                    return FString(Buf);
                }
                default:
                    return FString();
            }
        }

        // Skips _G/_ENV/_VERSION/_LOADED (anything underscore-prefixed).
        bool IsHiddenGlobal(const char* Name)
        {
            if (Name == nullptr) return true;
            return Name[0] == '_';
        }

        constexpr int kMaxHarvestDepth = 4;

        void IterateTable(lua_State* L, FStringView ParentPath, int Depth,
                          THashSet<const void*>& Visited, TVector<FLuaSymbol>& Out)
        {
            if (Depth >= kMaxHarvestDepth)
            {
                return;
            }

            // Cycle guard for recursive tables (metatable __index chains).
            const void* TablePtr = lua_topointer(L, -1);
            if (TablePtr != nullptr)
            {
                if (Visited.find(TablePtr) != Visited.end())
                {
                    return;
                }
                Visited.insert(TablePtr);
            }

            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                if (lua_type(L, -2) == LUA_TSTRING)
                {
                    const char* Key = lua_tostring(L, -2);

                    FLuaSymbol Symbol;
                    Symbol.Name.assign(Key);
                    Symbol.Kind = ClassifyTop(L);
                    Symbol.TypeName.assign(lua_typename(L, lua_type(L, -1)));
                    Symbol.ValuePreview = PreviewTopValue(L);
                    if (Symbol.Kind == ELuaSymbolKind::Function)
                    {
                        HarvestFunctionInfoTop(L, Symbol);
                    }
                    Symbol.Parent.assign(ParentPath.data(), ParentPath.size());
                    Symbol.Path = Symbol.Parent.empty()
                        ? Symbol.Name
                        : (Symbol.Parent + "." + Symbol.Name);
                    Out.push_back(Symbol);

                    if (Symbol.Kind == ELuaSymbolKind::Table)
                    {
                        IterateTable(L, FStringView(Symbol.Path.c_str(), Symbol.Path.size()),
                                     Depth + 1, Visited, Out);
                    }
                }
                lua_pop(L, 1);
            }
        }

        // Reads __lumina_members stamped by TClass<T>::Register so typed locals autocomplete.
        void HarvestReflectedMembers(lua_State* L, FStringView TypeName, TVector<FLuaSymbol>& Out)
        {
            luaL_getmetatable(L, TypeName.data());
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 1);
                return;
            }

            lua_rawgetfield(L, -1, "__lumina_members");
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 2);
                return;
            }

            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                if (lua_type(L, -2) == LUA_TSTRING)
                {
                    const char* Key = lua_tostring(L, -2);
                    // Skip underscore-prefixed bookkeeping (e.g. __parentname).
                    if (Key && Key[0] != '_')
                    {
                        const char* Kind = lua_isstring(L, -1) ? lua_tostring(L, -1) : "method";

                        FLuaSymbol Symbol;
                        Symbol.Name.assign(Key);
                        Symbol.Parent.assign(TypeName.data(), TypeName.size());
                        Symbol.Path = Symbol.Parent + "." + Symbol.Name;
                        if (Kind && std::strcmp(Kind, "method") == 0)
                        {
                            Symbol.Kind = ELuaSymbolKind::Function;
                            Symbol.TypeName.assign("function");
                            // __namecall-dispatched; opaque to lua_getinfo.
                            Symbol.bIsCFunction = true;
                            Symbol.bIsVararg    = true;
                        }
                        else
                        {
                            Symbol.Kind = ELuaSymbolKind::Value;
                            Symbol.TypeName.assign("property");
                        }
                        Out.push_back(Symbol);
                    }
                }
                lua_pop(L, 1);
            }

            lua_pop(L, 2);
        }
    }

    namespace
    {
        // Path = dotted name, Params = pipe-separated ("..." = vararg), Desc = tooltip line.
        struct FCuratedDoc
        {
            const char* Path;
            const char* Params; // pipe-separated, "" = no args, "..." = vararg
            const char* Desc;
        };

        const FCuratedDoc CuratedDocs[] =
        {
            // --- base library ---
            { "print",          "...",                  "Writes each argument to stdout, separated by tabs, followed by a newline." },
            { "tostring",       "v",                    "Converts v to a human-readable string. Honors the __tostring metamethod." },
            { "tonumber",       "v|base",               "Parses v into a number using the given base (default 10). Returns nil on failure." },
            { "type",           "v",                    "Returns the type of v as a string: \"nil\", \"number\", \"string\", \"boolean\", \"table\", \"function\", \"userdata\", \"thread\", \"vector\", \"buffer\"." },
            { "pairs",          "t",                    "Returns an iterator that yields each (key, value) pair of table t in unspecified order." },
            { "ipairs",         "t",                    "Returns an iterator that yields (i, t[i]) for i = 1, 2, ... until t[i] is nil." },
            { "next",           "t|key",                "Returns the next (key, value) after the given key in table t. Pass nil to start." },
            { "select",         "n|...",                "If n is \"#\", returns the number of varargs. Otherwise returns args from index n onward." },
            { "unpack",         "t|i|j",                "Returns t[i], t[i+1], ..., t[j]. Defaults: i = 1, j = #t. (Luau prefers table.unpack.)" },
            { "assert",         "v|message",            "Raises an error with the given message if v is nil or false; otherwise returns v unchanged." },
            { "error",          "message|level",        "Raises message as an error. Level controls which call frame is reported (default 1)." },
            { "pcall",          "f|...",                "Calls f in protected mode with the given args. Returns (true, results...) on success or (false, err) on error." },
            { "xpcall",         "f|handler|...",        "Like pcall but routes errors through handler before returning. Use to attach a stack traceback." },
            { "setmetatable",   "t|metatable",          "Sets the metatable of t to metatable (or nil to clear). Returns t." },
            { "getmetatable",   "object",               "Returns the metatable of object, or nil if it has none. Honors __metatable." },
            { "rawget",         "t|key",                "Reads t[key] without invoking the __index metamethod." },
            { "rawset",         "t|key|value",          "Writes t[key] = value without invoking the __newindex metamethod. Returns t." },
            { "rawequal",       "a|b",                  "Returns true if a and b are raw-equal (no __eq metamethod)." },
            { "rawlen",         "v",                    "Returns the raw length of a string or array part of a table. Ignores the __len metamethod." },
            { "collectgarbage", "opt|arg",              "Controls the garbage collector. opt: \"collect\", \"stop\", \"restart\", \"count\", \"step\", \"setpause\", \"setstepmul\"." },
            { "require",        "modname",              "Loads a module by name and returns its result. Uses package.loaded as a cache." },
            { "newproxy",       "withMeta",             "Creates a userdata proxy. If withMeta is true, the proxy has its own freshly-created metatable." },

            // math
            { "math.pi",        "",                     "The constant 3.141592653589793." },
            { "math.huge",      "",                     "Positive infinity. Larger than any other numeric value." },
            { "math.abs",       "x",                    "Returns the absolute value of x." },
            { "math.acos",      "x",                    "Returns the arc cosine of x (in radians)." },
            { "math.asin",      "x",                    "Returns the arc sine of x (in radians)." },
            { "math.atan",      "x",                    "Returns the arc tangent of x (in radians)." },
            { "math.atan2",     "y|x",                  "Returns atan(y / x) using the signs of both arguments to choose the correct quadrant." },
            { "math.ceil",      "x",                    "Returns the smallest integer >= x." },
            { "math.floor",     "x",                    "Returns the largest integer <= x." },
            { "math.cos",       "x",                    "Returns the cosine of x (x in radians)." },
            { "math.sin",       "x",                    "Returns the sine of x (x in radians)." },
            { "math.tan",       "x",                    "Returns the tangent of x (x in radians)." },
            { "math.cosh",      "x",                    "Returns the hyperbolic cosine of x." },
            { "math.sinh",      "x",                    "Returns the hyperbolic sine of x." },
            { "math.tanh",      "x",                    "Returns the hyperbolic tangent of x." },
            { "math.deg",       "x",                    "Converts radians to degrees." },
            { "math.rad",       "x",                    "Converts degrees to radians." },
            { "math.exp",       "x",                    "Returns e raised to the power of x." },
            { "math.log",       "x|base",               "Returns the natural logarithm of x, or log base `base` of x if provided." },
            { "math.log10",     "x",                    "Returns the base-10 logarithm of x." },
            { "math.pow",       "x|y",                  "Returns x raised to the power y. (Prefer the `^` operator.)" },
            { "math.sqrt",      "x",                    "Returns the square root of x." },
            { "math.fmod",      "x|y",                  "Returns the remainder of x / y, with the sign of x." },
            { "math.modf",      "x",                    "Splits x into integer and fractional parts. Returns (integer, fraction)." },
            { "math.frexp",     "x",                    "Returns (m, e) such that x = m * 2^e and 0.5 <= |m| < 1." },
            { "math.ldexp",     "m|e",                  "Returns m * 2^e." },
            { "math.max",       "x|...",                "Returns the largest of its arguments." },
            { "math.min",       "x|...",                "Returns the smallest of its arguments." },
            { "math.random",    "m|n",                  "With no args, returns a uniform random in [0,1). With m, returns an integer in [1,m]. With m,n, returns an integer in [m,n]." },
            { "math.randomseed","seed",                 "Seeds the pseudo-random number generator." },
            { "math.sign",      "x",                    "Returns -1, 0, or 1 depending on the sign of x. (Luau extension.)" },
            { "math.clamp",     "x|min|max",            "Returns x clamped to the inclusive range [min, max]. (Luau extension.)" },
            { "math.round",     "x",                    "Returns x rounded to the nearest integer (ties away from zero). (Luau extension.)" },
            { "math.noise",     "x|y|z",                "Perlin noise sample. 1, 2, or 3 arguments map to 1D/2D/3D noise. (Luau extension.)" },
            { "math.lerp",      "a|b|t",                "Linear interpolation: returns a + (b - a) * t. (Luau extension.)" },
            { "math.map",       "x|inMin|inMax|outMin|outMax", "Remaps x from [inMin, inMax] to [outMin, outMax]. (Luau extension.)" },

            // --- string library ---
            { "string.byte",    "s|i|j",                "Returns the byte values of s[i], s[i+1], ..., s[j]. Defaults: i = 1, j = i." },
            { "string.char",    "...",                  "Returns a string built from the given byte values." },
            { "string.find",    "s|pattern|init|plain", "Searches s for the first match of pattern. Returns (start, end[, captures...]) or nil." },
            { "string.format",  "fmt|...",              "Returns a string formatted printf-style. Supports %d %f %s %x %q and Luau's %* for tostring." },
            { "string.gmatch",  "s|pattern",            "Returns an iterator yielding each capture of pattern in s." },
            { "string.gsub",    "s|pattern|repl|n",     "Replaces up to n occurrences of pattern in s. repl can be a string, table, or function." },
            { "string.len",     "s",                    "Returns the byte length of s. Equivalent to #s." },
            { "string.lower",   "s",                    "Returns s with all uppercase letters converted to lowercase (ASCII)." },
            { "string.upper",   "s",                    "Returns s with all lowercase letters converted to uppercase (ASCII)." },
            { "string.match",   "s|pattern|init",       "Returns the first capture of pattern in s, or nil. If pattern has no captures, returns the whole match." },
            { "string.rep",     "s|n|sep",              "Returns n copies of s, optionally joined by sep." },
            { "string.reverse", "s",                    "Returns s with its bytes reversed." },
            { "string.sub",     "s|i|j",                "Returns the substring s[i..j]. Negative indices count from the end." },
            { "string.split",   "s|sep",                "Splits s on sep (default \",\") and returns a table of pieces. (Luau extension.)" },
            { "string.pack",    "fmt|...",              "Packs values into a binary string per fmt." },
            { "string.unpack",  "fmt|s|pos",            "Unpacks values from binary string s per fmt. Returns values plus first-unread index." },
            { "string.packsize","fmt",                  "Returns the size in bytes of strings produced by string.pack with this format." },

            // --- table library ---
            { "table.insert",   "t|pos|value",          "Inserts value at position pos in array t (default = end). Shifts later elements up by one." },
            { "table.remove",   "t|pos",                "Removes element at position pos from array t (default = end). Returns the removed element." },
            { "table.concat",   "t|sep|i|j",            "Concatenates t[i..j], joining adjacent elements with sep (default \"\")." },
            { "table.sort",     "t|comp",               "Sorts t in place. comp(a,b) should return true if a precedes b. Default is the < operator." },
            { "table.pack",     "...",                  "Returns {n = select('#', ...), ...}. Pairs with table.unpack to round-trip varargs." },
            { "table.unpack",   "t|i|j",                "Returns t[i], t[i+1], ..., t[j]. Defaults: i = 1, j = #t (or t.n)." },
            { "table.find",     "t|value|init",         "Returns the index of the first t[i] == value, or nil. (Luau extension.)" },
            { "table.clone",    "t",                    "Shallow-copies the array and hash parts of t. (Luau extension.)" },
            { "table.freeze",   "t",                    "Marks t and its current contents as read-only. Returns t. (Luau extension.)" },
            { "table.isfrozen", "t",                    "Returns true if t has been frozen with table.freeze. (Luau extension.)" },
            { "table.move",     "a1|f|e|t|a2",          "Copies a1[f..e] into a2[t..]. a2 defaults to a1." },
            { "table.create",   "count|value",          "Returns an array of `count` slots, each set to value (default nil). (Luau extension.)" },
            { "table.maxn",     "t",                    "Returns the largest positive numeric key in t, or 0 if there are none." },
            { "table.foreach",  "t|f",                  "Iterates t calling f(k, v). Deprecated; prefer pairs." },
            { "table.foreachi", "t|f",                  "Iterates the array part of t calling f(i, v). Deprecated; prefer ipairs." },
            { "table.getn",     "t",                    "Returns the array length of t. Deprecated; prefer #t." },

            // --- vector library (Luau native) ---
            { "vector.create",  "x|y|z",                "Returns a vector with components (x, y, z). Equivalent to vector(x, y, z)." },
            { "vector.zero",    "",                     "The zero vector (0, 0, 0)." },
            { "vector.one",     "",                     "The vector (1, 1, 1)." },
            { "vector.magnitude","v",                   "Returns the length of v: sqrt(v.x^2 + v.y^2 + v.z^2)." },
            { "vector.normalize","v",                   "Returns v scaled to unit length, or vector.zero if v is zero." },
            { "vector.dot",     "a|b",                  "Returns the dot product of a and b." },
            { "vector.cross",   "a|b",                  "Returns the cross product of a and b." },
            { "vector.angle",   "a|b|axis",             "Returns the unsigned angle between a and b in radians. axis disambiguates direction." },
            { "vector.max",     "v|...",                "Returns the component-wise max of two or more vectors." },
            { "vector.min",     "v|...",                "Returns the component-wise min of two or more vectors." },
            { "vector.floor",   "v",                    "Returns v with each component floored." },
            { "vector.ceil",    "v",                    "Returns v with each component ceilinged." },
            { "vector.abs",     "v",                    "Returns v with each component made absolute." },
            { "vector.sign",    "v",                    "Returns v with each component replaced by its sign (-1, 0, 1)." },
            { "vector.clamp",   "v|min|max",            "Returns v with each component clamped to the matching component of min and max." },
            { "vector.lerp",    "a|b|t",                "Component-wise linear interpolation between a and b." },

            // --- coroutine library ---
            { "coroutine.create",  "f",                 "Creates a new coroutine wrapping function f. Returns the coroutine handle (a thread)." },
            { "coroutine.resume",  "co|...",            "Starts or continues co. Returns (true, yielded values) or (false, error) on failure." },
            { "coroutine.yield",   "...",               "Suspends the current coroutine. The values become the return of the matching resume." },
            { "coroutine.status",  "co",                "Returns the coroutine's status: \"running\", \"suspended\", \"normal\", or \"dead\"." },
            { "coroutine.wrap",    "f",                 "Wraps f as a coroutine and returns a function that resumes it on each call." },
            { "coroutine.running", "",                  "Returns the running coroutine plus a bool indicating whether it's the main thread." },
            { "coroutine.isyieldable","",               "Returns true if the running coroutine can yield (false in the main thread / C call boundary)." },
            { "coroutine.close",   "co",                "Closes coroutine co, releasing its resources. Returns (true) or (false, err)." },

            // --- bit32 library ---
            { "bit32.band",     "...",                  "Returns the bitwise AND of all arguments (32-bit unsigned)." },
            { "bit32.bor",      "...",                  "Returns the bitwise OR of all arguments." },
            { "bit32.bxor",     "...",                  "Returns the bitwise XOR of all arguments." },
            { "bit32.bnot",     "x",                    "Returns the bitwise NOT of x (32-bit unsigned)." },
            { "bit32.btest",    "...",                  "Returns true if the bitwise AND of all arguments is non-zero." },
            { "bit32.lshift",   "x|n",                  "Shifts x left by n bits, zero-filling. Equivalent to x * 2^n mod 2^32." },
            { "bit32.rshift",   "x|n",                  "Shifts x right by n bits, zero-filling." },
            { "bit32.arshift",  "x|n",                  "Arithmetic right shift: shifts x right by n bits, sign-extending." },
            { "bit32.lrotate",  "x|n",                  "Rotates x left by n bits." },
            { "bit32.rrotate",  "x|n",                  "Rotates x right by n bits." },
            { "bit32.extract",  "x|field|width",        "Returns the unsigned `width` bits starting at `field` (default width = 1)." },
            { "bit32.replace",  "x|v|field|width",      "Returns x with bits [field..field+width-1] replaced by the low bits of v." },
            { "bit32.countlz",  "x",                    "Returns the number of leading zero bits in x. (Luau extension.)" },
            { "bit32.countrz",  "x",                    "Returns the number of trailing zero bits in x. (Luau extension.)" },

            // --- buffer library (Luau) ---
            { "buffer.create",     "size",              "Allocates a new buffer of the given size in bytes." },
            { "buffer.fromstring", "s",                 "Creates a buffer initialized from the bytes of s." },
            { "buffer.tostring",   "b",                 "Returns the bytes of b as a string." },
            { "buffer.len",        "b",                 "Returns the size of b in bytes." },
            { "buffer.copy",       "dst|dstOff|src|srcOff|count", "Copies count bytes from src[srcOff..] to dst[dstOff..]." },
            { "buffer.fill",       "b|offset|value|count","Fills count bytes of b starting at offset with byte value." },
            { "buffer.readi8",     "b|offset",          "Reads a signed 8-bit integer at offset." },
            { "buffer.readu8",     "b|offset",          "Reads an unsigned 8-bit integer at offset." },
            { "buffer.readi16",    "b|offset",          "Reads a signed 16-bit little-endian integer at offset." },
            { "buffer.readu16",    "b|offset",          "Reads an unsigned 16-bit little-endian integer at offset." },
            { "buffer.readi32",    "b|offset",          "Reads a signed 32-bit little-endian integer at offset." },
            { "buffer.readu32",    "b|offset",          "Reads an unsigned 32-bit little-endian integer at offset." },
            { "buffer.readf32",    "b|offset",          "Reads a 32-bit float at offset." },
            { "buffer.readf64",    "b|offset",          "Reads a 64-bit float at offset." },
            { "buffer.writei8",    "b|offset|value",    "Writes a signed 8-bit integer at offset." },
            { "buffer.writeu8",    "b|offset|value",    "Writes an unsigned 8-bit integer at offset." },
            { "buffer.writei16",   "b|offset|value",    "Writes a signed 16-bit little-endian integer at offset." },
            { "buffer.writeu16",   "b|offset|value",    "Writes an unsigned 16-bit little-endian integer at offset." },
            { "buffer.writei32",   "b|offset|value",    "Writes a signed 32-bit little-endian integer at offset." },
            { "buffer.writeu32",   "b|offset|value",    "Writes an unsigned 32-bit little-endian integer at offset." },
            { "buffer.writef32",   "b|offset|value",    "Writes a 32-bit float at offset." },
            { "buffer.writef64",   "b|offset|value",    "Writes a 64-bit float at offset." },
            { "buffer.readstring", "b|offset|count",    "Reads count bytes from b starting at offset and returns them as a string." },
            { "buffer.writestring","b|offset|s|count",  "Writes the bytes of s into b at offset. Defaults: count = #s." },

            // --- os library ---
            { "os.time",      "table",                  "Returns the current time as a Unix timestamp, or converts a date table to a timestamp." },
            { "os.date",      "format|time",            "Formats a timestamp using strftime-style format. Default format = \"%c\"." },
            { "os.clock",     "",                       "Returns CPU time used by the program in seconds." },
            { "os.difftime",  "t2|t1",                  "Returns t2 - t1 in seconds (Lua handles calendar quirks for non-Unix platforms)." },

            // --- utf8 library ---
            { "utf8.char",        "...",                "Returns a string made of the given codepoints." },
            { "utf8.codepoint",   "s|i|j",              "Returns codepoints of s[i..j]. Defaults: i = 1, j = i." },
            { "utf8.codes",       "s",                  "Iterator yielding (byte_offset, codepoint) for each character in s." },
            { "utf8.len",         "s|i|j",              "Returns the number of UTF-8 characters in s[i..j], or (false, byte_pos) on bad encoding." },
            { "utf8.offset",      "s|n|i",              "Returns the byte offset of the n-th character of s (counting from i)." },

            // --- debug library ---
            { "debug.traceback",  "thread|message|level","Returns a string with the current call stack. message and level are optional." },
            { "debug.info",       "thread|level|what",  "Like Lua's debug.getinfo but flat — returns the requested fields directly. (Luau)" },
            { "debug.getinfo",    "level|what",         "Returns a table describing the function at the given stack level." },
        };

        // O(N*M) but both small; runs only on editor focus.
        void ApplyCuratedDocs(TVector<FLuaSymbol>& Symbols)
        {
            for (FLuaSymbol& Symbol : Symbols)
            {
                for (const FCuratedDoc& Doc : CuratedDocs)
                {
                    if (std::strcmp(Symbol.Path.c_str(), Doc.Path) != 0)
                    {
                        continue;
                    }

                    if (Doc.Desc != nullptr && Doc.Desc[0] != '\0')
                    {
                        Symbol.Description.assign(Doc.Desc);
                    }

                    // "..." flips vararg; other segments become named params.
                    if (Doc.Params != nullptr && Doc.Params[0] != '\0')
                    {
                        Symbol.ParamNames.clear();
                        const char* P = Doc.Params;
                        while (*P != '\0')
                        {
                            const char* Start = P;
                            while (*P != '\0' && *P != '|') ++P;
                            const size_t Len = static_cast<size_t>(P - Start);
                            if (Len == 3 && std::memcmp(Start, "...", 3) == 0)
                            {
                                Symbol.bIsVararg = true;
                            }
                            else if (Len > 0)
                            {
                                Symbol.ParamNames.emplace_back(FString(Start, Len));
                            }
                            if (*P == '|') ++P;
                        }
                        Symbol.ParamCount = static_cast<uint8>(Symbol.ParamNames.size());
                        if (Symbol.Kind == ELuaSymbolKind::Function)
                        {
                            Symbol.bIsCFunction = false;
                        }
                    }
                    break;
                }
            }
        }
    }

    void FScriptingContext::HarvestGlobalSymbols(TVector<FLuaSymbol>& Out)
    {
        Out.clear();
        if (L == nullptr)
        {
            return;
        }

        FReadScopeLock Lock(SharedMutex);

        const int Top = lua_gettop(L);

        THashSet<const void*> Visited;

        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING)
            {
                const char* Key = lua_tostring(L, -2);
                if (!IsHiddenGlobal(Key))
                {
                    FLuaSymbol Symbol;
                    Symbol.Name.assign(Key);
                    Symbol.Kind = ClassifyTop(L);
                    Symbol.TypeName.assign(lua_typename(L, lua_type(L, -1)));
                    Symbol.ValuePreview = PreviewTopValue(L);
                    if (Symbol.Kind == ELuaSymbolKind::Function)
                    {
                        HarvestFunctionInfoTop(L, Symbol);
                    }
                    Symbol.Path = Symbol.Name;
                    Out.push_back(Symbol);

                    if (Symbol.Kind == ELuaSymbolKind::Table)
                    {
                        IterateTable(L, FStringView(Symbol.Path.c_str(), Symbol.Path.size()),
                                     /*Depth*/ 1, Visited, Out);

                        HarvestReflectedMembers(L, FStringView(Symbol.Name.c_str(), Symbol.Name.size()), Out);
                    }
                }
            }
            lua_pop(L, 1);
        }

        lua_settop(L, Top);

        ApplyCuratedDocs(Out);
    }
}
