#include "pch.h"
#include "Scripting.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include "lstate.h"
#include "luacode.h"
#include "lualib.h"
#include "ScriptTypes.h"
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
#include "World/World.h"
#include "UI/RmlUiBridge.h"
#include "Core/Application/Application.h"
#include "Events/EventProcessor.h"
#include "Input/InputMode.h"
#include "World/Entity/Systems/SystemContext.h"

namespace Lumina::Lua
{
    static void* ScriptingMemoryReallocFn([[maybe_unused]] void* Caller, void* Memory, [[maybe_unused]] size_t OldSize, size_t NewSize)
    {
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

    static int LuminaLuaPrint(lua_State* L)
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
        
        LOG_INFO("[Lua] - {}", Output);
        
        return 0;
    }
    
    static TUniquePtr<FScriptingContext> GScriptingContext;
    
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
    
    void FScriptingContext::Initialize()
    {
        L = lua_newstate(ScriptingMemoryReallocFn, this);
        
        lua_Callbacks* Callbacks    = lua_callbacks(L);
        Callbacks->useratom         = AtomString;
        Callbacks->panic            = LuaPanicHandler;

        luaL_openlibs(L);
        
        lua_pushcfunction(L, LuminaLuaPrint, "LuminaLuaPrint");
        lua_setglobal(L, "print");
        
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        FRef GlobalsRef(L, -1);
        
        CWorld::RegisterLuaModule(GlobalsRef);
        RmlUi::RegisterLuaModule(GlobalsRef);
        
        FRef EngineTable        = GlobalsRef.NewTable("Engine");
        FRef VFSTable           = EngineTable.NewTable("VFS");
        FRef MathTable          = EngineTable.NewTable("Math");
        FRef AudioTable         = EngineTable.NewTable("Audio");
        FRef FileHelperTable    = EngineTable.NewTable("FileHelper");
        FRef PathTable          = EngineTable.NewTable("Paths");
        FRef RHITable           = EngineTable.NewTable("RHI");
        FRef ECSTable           = EngineTable.NewTable("ECS");

        EngineTable.SetFunction<[](FStringView Name) { return StaticLoadObject(Name); } >("LoadObject");
        EngineTable.SetFunction<&FEngine::GetProjectName>("GetProjectName", GEngine);
        EngineTable.SetFunction<&FEngine::GetProjectPath>("GetProjectPath", GEngine);
        EngineTable.SetFunction<&FEngine::Travel>("Travel", GEngine);
        
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
        
        
        RHITable.SetFunction<&IRenderContext::CompileEngineShaders>("CompileEngineShaders", GRenderContext);
        RHITable.SetFunction<&IRenderContext::WaitIdle>("WaitIdle", GRenderContext);
        RHITable.SetFunction<&IRenderContext::GetAllocatedMemory>("GetAllocatedMemory", GRenderContext);
        RHITable.SetFunction<&IRenderContext::GetAvailableMemory>("GetAvailableMemory", GRenderContext);
        RHITable.SetFunction<&IRenderContext::SetVSyncEnabled>("SetVSyncEnabled");
        RHITable.SetFunction<&IRenderContext::IsVSyncEnabled>("IsVSyncEnabled");
        
        FileHelperTable.SetFunction<&FileHelper::CreateNewFile>("CreateNewFile");
        
        PathTable.SetFunction<&Paths::Exists>("Exists");
        PathTable.SetFunction<&Paths::GetEngineContentDirectory>("GetEngineContentDirectory");
        PathTable.SetFunction<&Paths::GetEngineConfigDirectory>("GetEngineConfigDirectory");
        PathTable.SetFunction<&Paths::GetEngineShadersDirectory>("GetEngineShadersDirectory");
        
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


        
        MathTable.Set("Pi",        glm::pi<float>());
        MathTable.Set("TwoPi",     glm::two_pi<float>());
        MathTable.Set("HalfPi",    glm::half_pi<float>());
        MathTable.Set("Epsilon",   glm::epsilon<float>());
        MathTable.Set("Infinity",  eastl::numeric_limits<float>::infinity());
        
        MathTable.SetFunction<[](glm::vec3 Target, glm::vec3 From) { return Math::FindLookAtRotation(Target, From); }>("FindLookAtRotation");

        MathTable.SetFunction<[](float X) { return glm::abs(X); }>("Abs");
        MathTable.SetFunction<[](float X) { return glm::sign(X); }>("Sign");
        MathTable.SetFunction<[](float X) { return glm::floor(X); }>("Floor");
        MathTable.SetFunction<[](float X) { return glm::ceil(X); }>("Ceil");
        MathTable.SetFunction<[](float X) { return glm::round(X); }>("Round");
        MathTable.SetFunction<[](float X) { return glm::fract(X); }>("Fract");
        MathTable.SetFunction<[](float X) { return glm::sqrt(X); }>("Sqrt");
        MathTable.SetFunction<[](float X) { return glm::inversesqrt(X); }>("InverseSqrt");
        MathTable.SetFunction<[](float X, float Y) { return glm::mod(X, Y); }>("Mod");
        MathTable.SetFunction<[](float X, float Y) { return glm::pow(X, Y); }>("Pow");
        MathTable.SetFunction<[](float X) { return glm::exp(X); }>("Exp");
        MathTable.SetFunction<[](float X) { return glm::log(X); }>("Log");
        MathTable.SetFunction<[](float X) { return glm::exp2(X); }>("Exp2");
        MathTable.SetFunction<[](float X) { return glm::log2(X); }>("Log2");
        
        MathTable.SetFunction<[](float A, float B, float T) { return glm::mix(A, B, T); }>("Lerp");
        MathTable.SetFunction<[](float X, float E0, float E1) { return glm::smoothstep(E0, E1, X); }>("SmoothStep");
        MathTable.SetFunction<[](float X, float E) { return glm::step(E, X); }>("Step");
        MathTable.SetFunction<[](float X, float Min, float Max) { return glm::clamp(X, Min, Max); }>("Clamp");
        MathTable.SetFunction<[](float A, float B) { return glm::min(A, B); }>("Min");
        MathTable.SetFunction<[](float A, float B) { return glm::max(A, B); }>("Max");
        
        MathTable.SetFunction<[](float X) { return glm::radians(X); }>("Radians");
        MathTable.SetFunction<[](float X) { return glm::degrees(X); }>("Degrees");
        MathTable.SetFunction<[](float X) { return glm::sin(X); }>("Sin");
        MathTable.SetFunction<[](float X) { return glm::cos(X); }>("Cos");
        MathTable.SetFunction<[](float X) { return glm::tan(X); }>("Tan");
        MathTable.SetFunction<[](float X) { return glm::asin(X); }>("Asin");
        MathTable.SetFunction<[](float X) { return glm::acos(X); }>("Acos");
        MathTable.SetFunction<[](float Y, float X) { return glm::atan(Y, X); }>("Atan2");
        
        MathTable.SetFunction<[](glm::vec2 A, glm::vec2 B) { return glm::dot(A, B); }>("Dot2");
        MathTable.SetFunction<[](glm::vec2 V) { return glm::length(V); }>("Length2");
        MathTable.SetFunction<[](glm::vec2 V) { return glm::normalize(V); }>("Normalize2");
        MathTable.SetFunction<[](glm::vec2 A, glm::vec2 B) { return glm::distance(A, B); }>("Distance2");
        MathTable.SetFunction<[](glm::vec2 A, glm::vec2 B, float T) { return glm::mix(A, B, T); }>("Lerp2");
        MathTable.SetFunction<[](glm::vec2 A, glm::vec2 B) { return glm::reflect(A, B); }>("Reflect2");
        
        MathTable.SetFunction<[](glm::vec3 A, glm::vec3 B) { return glm::dot(A, B); }>("Dot3");
        MathTable.SetFunction<[](glm::vec3 A, glm::vec3 B) { return glm::cross(A, B); }>("Cross");
        MathTable.SetFunction<[](glm::vec3 V) { return glm::length(V); }>("Length3");
        MathTable.SetFunction<[](glm::vec3 V) { return glm::normalize(V); }>("Normalize3");
        MathTable.SetFunction<[](glm::vec3 A, glm::vec3 B) { return glm::distance(A, B); }>("Distance3");
        MathTable.SetFunction<[](glm::vec3 A, glm::vec3 B, float T) { return glm::mix(A, B, T); }>("Lerp3");
        MathTable.SetFunction<[](glm::vec3 A, glm::vec3 B) { return glm::reflect(A, B); }>("Reflect3");
        MathTable.SetFunction<[](glm::vec3 A, glm::vec3 B, float Eta) { return glm::refract(A, B, Eta); }>("Refract3");
        
        MathTable.SetFunction<[](glm::vec4 A, glm::vec4 B) { return glm::dot(A, B); }>("Dot4");
        MathTable.SetFunction<[](glm::vec4 V) { return glm::length(V); }>("Length4");
        MathTable.SetFunction<[](glm::vec4 V) { return glm::normalize(V); }>("Normalize4");
        MathTable.SetFunction<[](glm::vec4 A, glm::vec4 B, float T) { return glm::mix(A, B, T); }>("Lerp4");
        
        MathTable.SetFunction<[](glm::quat A, glm::quat B) { return glm::dot(A, B); }>("QuatDot");
        MathTable.SetFunction<[](glm::quat A, glm::quat B, float T) { return glm::slerp(A, B, T); }>("QuatSlerp");
        MathTable.SetFunction<[](glm::quat Q) { return glm::normalize(Q); }>("QuatNormalize");
        MathTable.SetFunction<[](glm::quat Q) { return glm::inverse(Q); }>("QuatInverse");
        MathTable.SetFunction<[](glm::quat Q) { return glm::conjugate(Q); }>("QuatConjugate");
        MathTable.SetFunction<[](glm::quat Q) { return glm::eulerAngles(Q); }>("QuatEuler");
        MathTable.SetFunction<[](float Angle, glm::vec3 Axis) { return glm::angleAxis(Angle, Axis); }>("QuatAngleAxis");
        MathTable.SetFunction<[](glm::vec3 Euler) { return glm::quat(Euler); }>("QuatFromEuler");
        MathTable.SetFunction<[](glm::quat Q) { return glm::mat4_cast(Q); }>("QuatToMatrix");
        MathTable.SetFunction<[](glm::vec3 From, glm::vec3 To) { return glm::rotation(From, To); }>("QuatFromTo");
        MathTable.SetFunction<[](glm::quat Rot) { return glm::normalize(glm::rotate(Rot, glm::vec3(0.0f, 0.0f, 1.0f))); } >("QuatForward");
        
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
        // Accept strings so scripts read naturally: "Hidden", "Normal", "Captured".
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

        // Action queries resolve against the active viewport's FInputContext.
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

        // Returns an ID for UnbindAction. Scoped to whichever context is active at call time.
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


        AudioTable.SetFunction<[](FStringView File, glm::vec3 Location) { (void)GAudioContext->PlaySoundAtLocation(File, Location); }>("PlaySoundAtLocation");
        AudioTable.SetFunction<[](FStringView File) { (void)GAudioContext->PlaySound2D(File); }>("PlaySound2D");
        
        GlobalsRef.NewClass<FAudioHandle>("AudioHandle")
            .AddFunction<&FAudioHandle::IsValid>("IsValid")
            .Register();
        
        GlobalsRef.NewClass<glm::mat4>("Mat4")
            .Register();
        
        GlobalsRef.NewClass<glm::quat>("Quat")
            .AddProperty<&glm::quat::x>("X")
            .AddProperty<&glm::quat::y>("Y")
            .AddProperty<&glm::quat::z>("Z")
            .AddProperty<&glm::quat::w>("W")
    
            // Metamethods
            .AddFunction<[](glm::quat& A, glm::quat B) { return A * B; }>(EMetaMethod::Mul)
            .AddFunction<[](glm::quat& A, glm::quat B) { return A + B; }>(EMetaMethod::Add)
            .AddFunction<[](glm::quat& A, glm::quat B) { return A - B; }>(EMetaMethod::Sub)
            .AddFunction<[](glm::quat& A) { return -A; }>(EMetaMethod::UnaryMinus)
            .AddFunction<[](glm::quat& A, glm::quat B) { return A == B; }>(EMetaMethod::Eq)
            .AddFunction<[](glm::quat& Self) { return glm::to_string(Self); }>(EMetaMethod::ToString)
            .AddFunction<[](glm::quat& Self) { return glm::length(Self); }>(EMetaMethod::Len)
    
            // Methods
            .AddFunction<[](glm::quat& Self) { return glm::eulerAngles(Self); }>("EulerAngles")
            .AddFunction<[](glm::quat& Self) { return glm::normalize(Self); }>("Normalize")
            .AddFunction<[](glm::quat& Self) { return glm::inverse(Self); }>("Inverse")
            .AddFunction<[](glm::quat& Self) { return glm::length(Self); }>("Length")
            .AddFunction<[](glm::quat& Self) { return glm::conjugate(Self); }>("Conjugate")
            .AddFunction<[](glm::quat& Self) { return glm::dot(Self, Self); }>("LengthSquared")
            .AddFunction<[](glm::quat& A, glm::quat B) { return glm::dot(A, B); }>("Dot")
            .AddFunction<[](glm::quat& A, glm::quat B, float T) { return glm::slerp(A, B, T); }>("SLerp")
            .AddFunction<[](glm::quat& A, glm::quat B, float T) { return glm::lerp(A, B, T); }>("Lerp")
            .AddFunction<[](glm::quat& Self) { return glm::mat4_cast(Self); }>("ToMatrix")
            .AddFunction<[](float Angle, glm::vec3 Axis) { return glm::angleAxis(Angle, Axis); }>("FromAngleAxis")
            .AddFunction<[](glm::quat& Self, glm::vec3 V) { return Self * V; }>("RotateVector")
            .Register();
        

    }

    void FScriptingContext::SandboxGlobals()
    {
        luaL_sandbox(L);
    }

    void FScriptingContext::Shutdown()
    {
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
        
        DeferredActions.ProcessAllOf<FScriptLoad>([&](const FScriptLoad& Load)
        {
            OnScriptLoaded.Broadcast(Load.Path);
            ReloadScripts(Load.Path);
        });
    }
    
    int FScriptingContext::GetScriptMemoryUsageBytes() const
    {
        int KB        = lua_gc(L, LUA_GCCOUNT, 0);
        int Remainder = lua_gc(L, LUA_GCCOUNTB, 0);
        return (KB * 1024) + Remainder;
    }

    void FScriptingContext::ScriptReloaded(FStringView ScriptPath)
    {
        FWriteScopeLock Lock(SharedMutex);
        
        DeferredActions.EnqueueAction<FScriptLoad>(ScriptPath);
    }

    void FScriptingContext::ScriptCreated(FStringView ScriptPath)
    {
        FWriteScopeLock Lock(SharedMutex);
    }

    void FScriptingContext::ScriptRenamed(FStringView NewPath, FStringView OldPath)
    {
        FWriteScopeLock Lock(SharedMutex);
        
        DeferredActions.EnqueueAction<FScriptRename>(NewPath, OldPath);
    }

    void FScriptingContext::ScriptDeleted(FStringView ScriptPath)
    {
        FWriteScopeLock Lock(SharedMutex);
        
        DeferredActions.EnqueueAction<FScriptDelete>(ScriptPath);
    }

    TSharedPtr<FScript> FScriptingContext::LoadUniqueScriptPath(FStringView Path)
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
        
        FStringView FileName = VFS::FileName(Path);
        TSharedPtr<FScript> Script = LoadUniqueScript(ScriptData, FileName);
        if (Script == nullptr)
        {
            return {};
        }
        
        Script->Path = Path;
        RegisteredScripts[Path].emplace_back(Script);
        return Move(Script); // Won't elide.
    }

    TSharedPtr<FScript> FScriptingContext::LoadUniqueScript(FStringView Code, FStringView Name) const
    {
        LUMINA_PROFILE_SCOPE();
        
        size_t BytecodeSize = 0;
        lua_CompileOptions Options{};
        Options.debugLevel = 2;
        #ifndef DEBUG
        Options.optimizationLevel = 2;
        Options.debugLevel = 0;
        #endif
        char* Bytecode = luau_compile(Code.data(), Code.length(), &Options, &BytecodeSize);

        if (!Bytecode)
        {
            return {};
        }

        int StackBefore = lua_gettop(L);
        lua_State* Thread = lua_newthread(L);
        FRef ThreadRef(L, -1);
        
        luaL_sandboxthread(Thread);
        
        int LoadResult = luau_load(Thread, Name.data(), Bytecode, BytecodeSize, 0);
        free(Bytecode);

        if (LoadResult != LUA_OK)
        {
            LOG_ERROR("Lua - Failed to load: {}", lua_tostring(Thread, -1));
            return {};
        }

        int CallResult = lua_pcall(Thread, 0, 1, 0);
        if (CallResult != LUA_OK)
        {
            LOG_ERROR("Runtime Error {}", lua_tostring(Thread, -1));
            return {};
        }
        
        // Duck-type the schema + defaults by walking the `Exports` field of the returned module
        // table. We do this while the module table is still on top of the Thread stack.
        FScriptExportSchema Schema;
        TVector<FScriptPropertyEntry> Defaults;
        if (lua_istable(Thread, -1))
        {
            lua_getfield(Thread, -1, "Exports");
            if (lua_istable(Thread, -1))
            {
                BuildSchemaFromExportsTable(Thread, -1, Schema, Defaults);
            }
            lua_pop(Thread, 1);
        }

        auto NewScript = MakeShared<FScript>();
        NewScript->Name             = Name;
        NewScript->Path             = "";
        NewScript->Reference        = FRef(Thread, -1);
        NewScript->ExportsSchema    = eastl::move(Schema);
        NewScript->ExportDefaults   = eastl::move(Defaults);

        lua_pushvalue(Thread, LUA_GLOBALSINDEX);
        NewScript->Environment  = FRef(Thread, -1);
        NewScript->Thread       = ThreadRef;
        
        int StackAfter = lua_gettop(L);

        DEBUG_ASSERT(StackAfter == StackBefore);
        
        return NewScript;
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
    }

    // ------------------------------------------------------------------------
    // Symbol harvesting

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

        // Skips Lua/Luau builtins so suggestions don't drown in `_G`, `_VERSION`,
        // `string`, `table`, etc. We could surface these too, but for engine-script
        // editing the noise hurts more than it helps.
        bool IsBuiltinName(const char* Name)
        {
            if (Name == nullptr) return true;
            if (Name[0] == '_') return true;  // _G, _VERSION, _ENV, _LOADED
            const char* const Builtins[] = {
                "string", "table", "math", "os", "io", "coroutine", "debug",
                "bit32", "utf8", "buffer", "vector",
                "assert", "collectgarbage", "error", "getfenv", "getmetatable",
                "ipairs", "next", "pairs", "pcall", "rawequal", "rawget",
                "rawset", "rawlen", "select", "setfenv", "setmetatable",
                "tonumber", "tostring", "type", "unpack", "xpcall",
                "newproxy", "require", "loadstring",
            };
            for (const char* B : Builtins)
            {
                if (std::strcmp(Name, B) == 0) return true;
            }
            return false;
        }

        void IterateTableShallow(lua_State* L, FStringView ParentPath, TVector<FLuaSymbol>& Out)
        {
            // Caller has the table on top of the stack.
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                // Stack: ... table key value
                if (lua_type(L, -2) == LUA_TSTRING)
                {
                    const char* Key = lua_tostring(L, -2);

                    FLuaSymbol Symbol;
                    Symbol.Name.assign(Key);
                    Symbol.Kind = ClassifyTop(L);
                    Symbol.Parent.assign(ParentPath.data(), ParentPath.size());
                    Symbol.Path = Symbol.Parent.empty()
                        ? Symbol.Name
                        : (Symbol.Parent + "." + Symbol.Name);
                    Out.push_back(Symbol);
                }
                lua_pop(L, 1);  // pop value, keep key for next iter
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

        // Top-level pass over LUA_GLOBALSINDEX.
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING)
            {
                const char* Key = lua_tostring(L, -2);
                if (!IsBuiltinName(Key))
                {
                    FLuaSymbol Symbol;
                    Symbol.Name.assign(Key);
                    Symbol.Kind = ClassifyTop(L);
                    Symbol.Path = Symbol.Name;
                    Out.push_back(Symbol);

                    // One level of nesting for tables: covers Engine.VFS,
                    // Input.IsKeyDown, etc. Deep recursion would catch
                    // metatables / class chains; skip for now to keep
                    // the symbol set shallow and predictable.
                    if (Symbol.Kind == ELuaSymbolKind::Table)
                    {
                        IterateTableShallow(L, Symbol.Path, Out);
                    }
                }
            }
            lua_pop(L, 1);
        }

        lua_settop(L, Top);
    }
}
