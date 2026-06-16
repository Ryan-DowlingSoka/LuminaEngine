#include "DotNetHost.h"
#include "ManagedCall.h"

#include <filesystem>
#include <cstring>
#include <cstdio>

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Plugin/Plugin.h"
#include "Core/Plugin/PluginManager.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Paths/Paths.h"
#include "Platform/Process/PlatformProcess.h"
#include "World/World.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/Component.h"
#include "Scripting/ScriptExports.h"
#include "Scripting/ScriptPropertyOverrides.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/SoftObjectPtr.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "TaskSystem/ThreadedCallback.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <tlhelp32.h>
#else
    #include <dlfcn.h>
    #include <link.h>
    #include <cstring>
#endif

#include "coreclr_delegates.h"
#include "hostfxr.h"

namespace Lumina::DotNet
{
    namespace
    {
    #if defined(_WIN32)
        #define LSTR(s) L##s
        constexpr const char* kSharedExt = ".dll";
    #elif defined(__APPLE__)
        #define LSTR(s) s
        constexpr const char* kSharedExt = ".dylib";
    #else
        #define LSTR(s) s
        constexpr const char* kSharedExt = ".so";
    #endif

        namespace fs = std::filesystem;

        // Native<->managed boundary. The managed Host mirrors this layout exactly;
        // any change here bumps GAbiVersion and the matching managed constant.
        struct FExporterTable
        {
            void (CORECLR_DELEGATE_CALLTYPE* Log)(int32 Level, const char* Utf8, int32 Len);
        };

        struct FBootstrapArgs
        {
            int32                   AbiVersion;
            const FExporterTable*   Exports;
            void*                   NativeModule;   // Runtime.dll handle; managed resolves "LuminaNative" to it
        };

        // One script source handed to managed. Mirrors LuminaSharp.SourceFile (natural x64 layout).
        struct FSourceFile
        {
            const char* Path;
            int32       PathLen;
            const char* Text;
            int32       TextLen;
        };

        typedef int32 (CORECLR_DELEGATE_CALLTYPE* BootstrapFn)(const FBootstrapArgs*);
        typedef int32 (CORECLR_DELEGATE_CALLTYPE* LoadScriptsFn)(const FSourceFile*, int32);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* TickFn)();
        typedef void  (CORECLR_DELEGATE_CALLTYPE* ShutdownFn)();
        typedef int32 (CORECLR_DELEGATE_CALLTYPE* GetGenerationFn)();
        typedef void* (CORECLR_DELEGATE_CALLTYPE* CreateEntityScriptFn)(const char*, int32, uint64, uint32);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* OnReadyScriptFn)(void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* UpdateScriptsFn)(void* const*, int32, float);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* DestroyEntityScriptFn)(void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* EnumerateEntityScriptsFn)(void*, void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* EnumerateEntitySystemsFn)(void*, void*);
        typedef void* (CORECLR_DELEGATE_CALLTYPE* CreateEntitySystemFn)(const char*, int32, uint64);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* TickEntitySystemFn)(void*, void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* DestroyEntitySystemFn)(void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* DispatchCollisionFn)(void*, int32, const void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* DispatchInputFn)(void*, int32, int32, int32, int32, int32, double, double, double, double, double);
        typedef int32 (CORECLR_DELEGATE_CALLTYPE* GetCallbackFlagsFn)(void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* GetScriptSchemaFn)(const char*, int32, void*, void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* ApplyScriptPropertiesFn)(void*, const uint8*, int32);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* InvokeAssetCallbackFn)(void*, void*);
        typedef void* (CORECLR_DELEGATE_CALLTYPE* ManagedClassFindFn)(const char*, int32);
        typedef void* (CORECLR_DELEGATE_CALLTYPE* ManagedObjectNewFn)(void*);
        typedef void  (CORECLR_DELEGATE_CALLTYPE* ManagedFreeHandleFn)(void*);
        typedef int32 (CORECLR_DELEGATE_CALLTYPE* ManagedInvokeFn)(void*, uint8, const char*, int32, const uint8*, int32, void*, void*);
        typedef int32 (CORECLR_DELEGATE_CALLTYPE* ManagedFieldGetFn)(void*, const char*, int32, void*, void*);
        typedef int32 (CORECLR_DELEGATE_CALLTYPE* ManagedFieldSetFn)(void*, const char*, int32, const uint8*, int32);

        bool                                        bInitialized = false;
        hostfxr_get_runtime_delegate_fn             GGetDelegate = nullptr;
        FExporterTable                              GExports{};
        LoadScriptsFn                               GLoadScripts = nullptr;
        TickFn                                      GManagedTick = nullptr;
        ShutdownFn                                  GManagedShutdown = nullptr;
        GetGenerationFn                             GGetGeneration = nullptr;
        int32                                       GCachedGeneration = 0;
        CreateEntityScriptFn                        GCreateEntityScript = nullptr;
        OnReadyScriptFn                             GOnReadyScript = nullptr;
        UpdateScriptsFn                             GUpdateScripts = nullptr;
        DestroyEntityScriptFn                       GDestroyEntityScript = nullptr;
        EnumerateEntityScriptsFn                    GEnumerateEntityScripts = nullptr; // optional (editor discovery)
        EnumerateEntitySystemsFn                    GEnumerateEntitySystems = nullptr; // optional (C# systems)
        CreateEntitySystemFn                        GCreateEntitySystem = nullptr;     // optional (C# systems)
        TickEntitySystemFn                          GTickEntitySystem = nullptr;       // optional (C# systems)
        DestroyEntitySystemFn                       GDestroyEntitySystem = nullptr;    // optional (C# systems)
        DispatchCollisionFn                         GDispatchCollision = nullptr;      // optional (collision callbacks)
        DispatchInputFn                             GDispatchInput = nullptr;          // optional (OnInput events)
        GetCallbackFlagsFn                          GGetCallbackFlags = nullptr;       // optional
        GetScriptSchemaFn                           GGetScriptSchema = nullptr;        // optional ([Property] schema)
        ApplyScriptPropertiesFn                     GApplyScriptProperties = nullptr;  // optional ([Property] apply)
        InvokeAssetCallbackFn                       GInvokeAssetCallback = nullptr;    // optional (Asset.LoadAsync)
        ManagedClassFindFn                          GManagedClassFind = nullptr;       // optional (dynamic invoke)
        ManagedObjectNewFn                          GManagedObjectNew = nullptr;       // optional
        ManagedFreeHandleFn                         GManagedFreeHandle = nullptr;      // optional
        ManagedInvokeFn                             GManagedInvoke = nullptr;          // optional
        ManagedFieldGetFn                           GManagedFieldGet = nullptr;        // optional
        ManagedFieldSetFn                           GManagedFieldSet = nullptr;        // optional

        // Sink the managed EnumerateEntityScripts calls once per script type; Ctx is the out vector.
        void LmScriptNameSink(void* Ctx, const char* Name, int Len)
        {
            auto* Out = static_cast<TVector<FString>*>(Ctx);
            if (Out != nullptr && Name != nullptr && Len > 0)
            {
                Out->emplace_back(FString(Name, static_cast<size_t>(Len)));
            }
        }

        // Sink the managed EnumerateEntitySystems calls once per system type; Ctx is the out vector.
        void LmSystemDescSink(void* Ctx, const char* Name, int Len, int Stage, int Priority)
        {
            auto* Out = static_cast<TVector<FManagedSystemDesc>*>(Ctx);
            if (Out != nullptr && Name != nullptr && Len > 0)
            {
                FManagedSystemDesc Desc;
                Desc.TypeName.assign(Name, static_cast<size_t>(Len));
                Desc.Stage = (Stage >= 0 && Stage < (int)EUpdateStage::Max) ? (EUpdateStage)Stage : EUpdateStage::PrePhysics;
                Desc.Priority = Priority;
                Out->push_back(eastl::move(Desc));
            }
        }

        // Keeps each source file's text alive while it's marshaled to managed.
        struct FGatheredSource
        {
            FString Path;
            FString Text;
        };

        void GatherSourcesUnder(FStringView Root, TVector<FGatheredSource>& Out)
        {
            VFS::RecursiveDirectoryIterator(Root, [&Out](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory() || Info.GetExt() != ".cs")
                {
                    return;
                }
                const FStringView VirtualPath(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (VirtualPath.find("/obj/") != FStringView::npos || VirtualPath.find("/bin/") != FStringView::npos)
                {
                    return;
                }
                FGatheredSource Src;
                Src.Path.assign(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (VFS::ReadFile(Src.Text, Info.VirtualPath))
                {
                    Out.push_back(eastl::move(Src));
                }
            });
        }
        
        void WriteScriptProject(FStringView ScriptsDirView, FStringView Label, const FString& LuminaSharpDll)
        {
            FString ScriptsDir(ScriptsDirView.data(), ScriptsDirView.size());
            if (!VFS::IsDirectory(ScriptsDir))
            {
                return;
            }

            FString LabelStr(Label.data(), Label.size());

            FString Xml;
            Xml += "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
            Xml += "  <!-- GENERATED for IDE IntelliSense only (run \"dotnet.genprojects\" to refresh).\n";
            Xml += "       Game scripts are compiled at runtime by the engine; this project is never the\n";
            Xml += "       runtime path. Edits are overwritten. -->\n";
            Xml += "  <PropertyGroup>\n";
            Xml += "    <TargetFramework>net10.0</TargetFramework>\n";
            Xml += "    <Nullable>enable</Nullable>\n";
            Xml += "    <ImplicitUsings>disable</ImplicitUsings>\n";
            Xml += "    <EnableDefaultItems>true</EnableDefaultItems>\n";
            Xml += "  </PropertyGroup>\n";
            Xml += "  <ItemGroup>\n";
            Xml += "    <Reference Include=\"LuminaSharp\">\n";
            Xml += "      <HintPath>" + LuminaSharpDll + "</HintPath>\n";
            Xml += "    </Reference>\n";
            Xml += "  </ItemGroup>\n";
            Xml += "</Project>\n";

            FString OutPath = ScriptsDir + "/" + LabelStr + ".Scripts.csproj";

            // Idempotent: skip the write when the on-disk project already matches, so frequent reloads don't
            // churn the file (and the IDE doesn't keep reloading the project on every script hot-reload).
            FString Existing;
            if (VFS::ReadFile(Existing, OutPath) && Existing == Xml)
            {
                return;
            }

            if (VFS::WriteFile(OutPath, FStringView(Xml.c_str(), Xml.size())))
            {
                LOG_DISPLAY("Generated C# script project: {}", OutPath.c_str());
            }
        }

        // Native function the managed side calls to write into the engine log.
        void CORECLR_DELEGATE_CALLTYPE Export_Log(int32 Level, const char* Utf8, int32 Len)
        {
            const FString Msg(Utf8, (size_t)Len);
            switch (Level)
            {
            case 0:  LOG_TRACE("[C#] {}", Msg.c_str()); break;
            case 2:  LOG_WARN ("[C#] {}", Msg.c_str()); break;
            case 3:  LOG_ERROR("[C#] {}", Msg.c_str()); break;
            default: LOG_INFO ("[C#] {}", Msg.c_str()); break;
            }
        }
        
        fs::path NativePath(fs::path P)
        {
            P.make_preferred();
            return P;
        }

        const char* RuntimeRid()
        {
        #if defined(_WIN32)
            return "win-x64";
        #elif defined(__APPLE__)
            return "osx-x64";
        #else
            return "linux-x64";
        #endif
        }

        void* LoadShared(const fs::path& Path)
        {
        #if defined(_WIN32)
            return (void*)::LoadLibraryW(Path.c_str());
        #else
            return ::dlopen(Path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        #endif
        }

        void* GetSym(void* Lib, const char* Name)
        {
        #if defined(_WIN32)
            return (void*)::GetProcAddress((HMODULE)Lib, Name);
        #else
            return ::dlsym(Lib, Name);
        #endif
        }

        // Handle of the module this code lives in (Runtime.dll), where the generated property thunks
        // are exported. Managed maps the "LuminaNative" P/Invoke library name to it.
        void* GetThunkModuleHandle()
        {
        #if defined(_WIN32)
            HMODULE Module = nullptr;
            ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&Export_Log), &Module);
            return (void*)Module;
        #else
            return nullptr; // POSIX: dladdr-based, added with the POSIX port
        #endif
        }
    }

    void Initialize()
    {
        if (bInitialized)
        {
            return;
        }

        const fs::path Install = fs::path(Paths::GetEngineInstallDirectory().c_str());
        const fs::path Bundled = NativePath(Install / "External" / "DotNet" / "runtime" / RuntimeRid());
        if (!fs::exists(Bundled))
        {
            LOG_ERROR("C# scripting disabled: bundled .NET runtime not found at '{}'. Run Setup.bat to extract External.", Bundled.string().c_str());
            return;
        }

        // Locate host/fxr/<version>/hostfxr.<ext> under the bundled root.
        fs::path HostfxrPath;
        std::error_code Ec;
        for (const auto& Entry : fs::directory_iterator(Bundled / "host" / "fxr", Ec))
        {
            const fs::path Candidate = Entry.path() / (FString("hostfxr") + kSharedExt).c_str();
            if (fs::exists(Candidate))
            {
                HostfxrPath = NativePath(Candidate);
                break;
            }
        }
        if (HostfxrPath.empty())
        {
            LOG_ERROR("C# scripting disabled: hostfxr not found under '{}'.", (Bundled / "host" / "fxr").string().c_str());
            return;
        }

        void* Lib = LoadShared(HostfxrPath);
        if (!Lib)
        {
            LOG_ERROR("C# scripting disabled: failed to load hostfxr at '{}'.", HostfxrPath.string().c_str());
            return;
        }

        auto Init  = (hostfxr_initialize_for_runtime_config_fn)GetSym(Lib, "hostfxr_initialize_for_runtime_config");
        GGetDelegate = (hostfxr_get_runtime_delegate_fn)       GetSym(Lib, "hostfxr_get_runtime_delegate");
        auto Close = (hostfxr_close_fn)                        GetSym(Lib, "hostfxr_close");
        if (!Init || !GGetDelegate || !Close)
        {
            LOG_ERROR("C# scripting disabled: hostfxr is missing expected exports.");
            return;
        }

        // Managed bootstrap, built next to the binaries by the LuminaSharpManaged project.
        const fs::path ExePath      = NativePath(fs::path(Platform::GetCurrentProcessPath().c_str()));
        const fs::path ManagedDir   = ExePath.parent_path() / "DotNet" / "Managed";
        const fs::path BootstrapDll = NativePath(ManagedDir / "LuminaSharp.dll");
        const fs::path BootstrapCfg = NativePath(ManagedDir / "LuminaSharp.runtimeconfig.json");
        if (!fs::exists(BootstrapDll) || !fs::exists(BootstrapCfg))
        {
            LOG_ERROR("C# scripting disabled: managed bootstrap missing under '{}'. Did LuminaSharpManaged build?", ManagedDir.string().c_str());
            return;
        }

        hostfxr_initialize_parameters Params{};
        Params.size = sizeof(Params);
        Params.host_path = ExePath.c_str();
        Params.dotnet_root = Bundled.c_str();

        hostfxr_handle Ctx = nullptr;
        int rc = Init(BootstrapCfg.c_str(), &Params, &Ctx);
        if (rc != 0 || Ctx == nullptr)
        {
            LOG_ERROR("C# scripting disabled: hostfxr_initialize_for_runtime_config failed (0x{:x}).", (uint32)rc);
            if (Ctx)
            {
                Close(Ctx);
            }
            return;
        }

        void* LoadAsmFn = nullptr;
        rc = GGetDelegate(Ctx, hdt_load_assembly_and_get_function_pointer, &LoadAsmFn);
        Close(Ctx);
        if (rc != 0 || LoadAsmFn == nullptr)
        {
            LOG_ERROR("C# scripting disabled: get_runtime_delegate failed (0x{:x}).", (uint32)rc);
            return;
        }

        auto LoadAssembly = (load_assembly_and_get_function_pointer_fn)LoadAsmFn;

        const auto* HostType = LSTR("LuminaSharp.Host, LuminaSharp");

        BootstrapFn Bootstrap = nullptr;
        rc = LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("Bootstrap"), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&Bootstrap);
        if (rc != 0 || Bootstrap == nullptr)
        {
            LOG_ERROR("C# scripting disabled: failed to resolve managed Bootstrap entry (0x{:x}).", (uint32)rc);
            return;
        }

        // Resolve the rest of the managed entry surface up front.
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("LoadScripts"),         UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GLoadScripts);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("Tick"),                UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedTick);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("Shutdown"),            UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedShutdown);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("GetGeneration"),       UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GGetGeneration);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("CreateEntityScript"),  UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GCreateEntityScript);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("OnReadyScript"),       UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GOnReadyScript);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("UpdateScripts"),       UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GUpdateScripts);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("DestroyEntityScript"), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GDestroyEntityScript);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("EnumerateEntityScripts"), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GEnumerateEntityScripts);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("DispatchCollision"),      UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GDispatchCollision);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("DispatchInput"),          UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GDispatchInput);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("EnumerateEntitySystems"), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GEnumerateEntitySystems);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("CreateEntitySystem"),     UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GCreateEntitySystem);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("TickEntitySystem"),       UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GTickEntitySystem);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("DestroyEntitySystem"),    UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GDestroyEntitySystem);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("GetScriptCallbackFlags"), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GGetCallbackFlags);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("GetScriptSchema"),        UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GGetScriptSchema);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("ApplyScriptProperties"),  UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GApplyScriptProperties);
        LoadAssembly(BootstrapDll.c_str(), HostType, LSTR("InvokeAssetCallback"),    UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GInvokeAssetCallback);

        const auto* CallsType = LSTR("LuminaSharp.ManagedCalls, LuminaSharp");
        LoadAssembly(BootstrapDll.c_str(), CallsType, LSTR("ClassFind"),  UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedClassFind);
        LoadAssembly(BootstrapDll.c_str(), CallsType, LSTR("ObjectNew"),  UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedObjectNew);
        LoadAssembly(BootstrapDll.c_str(), CallsType, LSTR("FreeHandle"), UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedFreeHandle);
        LoadAssembly(BootstrapDll.c_str(), CallsType, LSTR("Invoke"),     UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedInvoke);
        LoadAssembly(BootstrapDll.c_str(), CallsType, LSTR("FieldGet"),   UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedFieldGet);
        LoadAssembly(BootstrapDll.c_str(), CallsType, LSTR("FieldSet"),   UNMANAGEDCALLERSONLY_METHOD, nullptr, (void**)&GManagedFieldSet);

        if (GLoadScripts == nullptr || GManagedTick == nullptr || GManagedShutdown == nullptr ||
            GGetGeneration == nullptr || GCreateEntityScript == nullptr ||
            GUpdateScripts == nullptr || GDestroyEntityScript == nullptr)
        {
            LOG_ERROR("C# scripting disabled: failed to resolve managed script entry points.");
            return;
        }

        GExports.Log = &Export_Log;

        FBootstrapArgs Args{};
        Args.AbiVersion = GAbiVersion;
        Args.Exports = &GExports;
        Args.NativeModule = GetThunkModuleHandle();

        const int32 Result = Bootstrap(&Args);
        if (Result != 0)
        {
            LOG_ERROR("C# scripting disabled: managed bootstrap handshake failed (returned {}). ABI mismatch?", Result);
            return;
        }

        bInitialized = true;
        LOG_DISPLAY(".NET host initialized (bundled runtime: {}).", Bundled.string().c_str());
    }

    void Shutdown()
    {
        if (!bInitialized)
        {
            return;
        }

        if (GManagedShutdown)
        {
            GManagedShutdown();
        }

        bInitialized = false;
        GExports = FExporterTable{};
        GLoadScripts = nullptr;
        GManagedTick = nullptr;
        GManagedShutdown = nullptr;
        GGetGeneration = nullptr;
        GCachedGeneration = 0;
        GCreateEntityScript = nullptr;
        GOnReadyScript = nullptr;
        GUpdateScripts = nullptr;
        GDestroyEntityScript = nullptr;
        GEnumerateEntityScripts = nullptr;
        GEnumerateEntitySystems = nullptr;
        GCreateEntitySystem = nullptr;
        GTickEntitySystem = nullptr;
        GDestroyEntitySystem = nullptr;
        GDispatchCollision = nullptr;
        GDispatchInput = nullptr;
        GGetCallbackFlags = nullptr;
        GGetScriptSchema = nullptr;
        GApplyScriptProperties = nullptr;
        GInvokeAssetCallback = nullptr;
        GManagedClassFind = nullptr;
        GManagedObjectNew = nullptr;
        GManagedFreeHandle = nullptr;
        GManagedInvoke = nullptr;
        GManagedFieldGet = nullptr;
        GManagedFieldSet = nullptr;
        LOG_DISPLAY(".NET host shut down.");
    }

    void Tick()
    {
        if (!bInitialized || GManagedTick == nullptr)
        {
            return;
        }
        GManagedTick();
    }

    void ReloadScripts()
    {
        if (!bInitialized || GLoadScripts == nullptr)
        {
            return;
        }

        // Gather every Scripts/*.cs across all mounted roots: project, each enabled plugin's
        // content mount, then the engine library.
        TVector<FGatheredSource> Sources;
        GatherSourcesUnder("/Game/Scripts", Sources);
        for (const FPlugin* Plugin : FPluginManager::Get().GetAllPlugins())
        {
            if (Plugin == nullptr || !Plugin->IsEnabled() || !Plugin->IsContentMounted())
            {
                continue;
            }
            FString Root = Plugin->GetMountAlias();
            Root += "/Scripts";
            GatherSourcesUnder(FStringView(Root.c_str(), Root.size()), Sources);
        }
        GatherSourcesUnder("/Engine/Resources/Scripts", Sources);

        TVector<FSourceFile> Marshaled;
        Marshaled.reserve(Sources.size());
        for (const FGatheredSource& S : Sources)
        {
            FSourceFile File;
            File.Path    = S.Path.c_str();
            File.PathLen = (int32)S.Path.size();
            File.Text    = S.Text.c_str();
            File.TextLen = (int32)S.Text.size();
            Marshaled.push_back(File);
        }

        LOG_DISPLAY("C#: compiling {} script file(s)...", Marshaled.size());
        const int32 Result = GLoadScripts(Marshaled.empty() ? nullptr : Marshaled.data(), (int32)Marshaled.size());
        if (Result != 0)
        {
            LOG_ERROR("C# script load/reload returned error {}.", Result);
        }

        // Refresh the native generation mirror once here (the only place it can change), so the
        // per-frame tick never crosses the boundary to read it.
        GCachedGeneration = GGetGeneration ? GGetGeneration() : GCachedGeneration;

        // Keep the IDE projects in lockstep with the scripts that just (re)loaded so an absent or deleted
        // .csproj self-heals on ANY reload, not only on a full project load (idempotent; no-op if unchanged).
        GenerateScriptProjects();
    }

    void GenerateScriptProjects()
    {
        if (!bInitialized)
        {
            return;
        }

        const fs::path ExePath = NativePath(fs::path(Platform::GetCurrentProcessPath().c_str()));
        const FString Dll((ExePath.parent_path() / "DotNet" / "Managed" / "LuminaSharp.dll").string().c_str());

        // Game scripts live under /Game/Scripts (sibling of /Game/Content); plugins keep theirs under
        // <Mount>/Scripts. The label becomes the .csproj name.
        WriteScriptProject("/Game/Scripts", "Game", Dll);
        for (const FPlugin* Plugin : FPluginManager::Get().GetAllPlugins())
        {
            if (Plugin == nullptr || !Plugin->IsEnabled() || !Plugin->IsContentMounted())
            {
                continue;
            }
            const FString Alias = Plugin->GetMountAlias();
            FString ScriptsDir = Alias + "/Scripts";
            FStringView Label(Alias.c_str(), Alias.size());
            if (!Label.empty() && Label.front() == '/')
            {
                Label.remove_prefix(1);
            }
            WriteScriptProject(FStringView(ScriptsDir.c_str(), ScriptsDir.size()), Label, Dll);
        }

        // Engine library/example scripts live under /Engine/Resources/Scripts (sibling of the engine's
        // Content, mirroring a game project's Content/Scripts split) so the engine mounts properly.
        WriteScriptProject("/Engine/Resources/Scripts", "Engine", Dll);
    }

    int32 GetScriptGeneration()
    {
        return GCachedGeneration; // native mirror; refreshed on (re)load, see ReloadScripts
    }

    void* CreateEntityScript(FStringView TypeName, uint64 World, uint32 Entity)
    {
        if (!bInitialized || GCreateEntityScript == nullptr)
        {
            return nullptr;
        }
        return GCreateEntityScript(TypeName.data(), (int32)TypeName.size(), World, Entity);
    }

    void OnReadyScript(void* Instance)
    {
        if (bInitialized && GOnReadyScript && Instance)
        {
            GOnReadyScript(Instance);
        }
    }

    void UpdateScripts(void* const* Instances, int32 Count, float DeltaSeconds)
    {
        if (bInitialized && GUpdateScripts && Count > 0)
        {
            GUpdateScripts(Instances, Count, DeltaSeconds);
        }
    }

    void DestroyEntityScript(void* Instance)
    {
        if (bInitialized && GDestroyEntityScript && Instance)
        {
            GDestroyEntityScript(Instance);
        }
    }

    void GatherEntityScriptTypes(TVector<FString>& OutTypeNames)
    {
        OutTypeNames.clear();
        if (bInitialized && GEnumerateEntityScripts)
        {
            GEnumerateEntityScripts(reinterpret_cast<void*>(&LmScriptNameSink), &OutTypeNames);
        }
    }

    void GatherManagedSystemDescs(TVector<FManagedSystemDesc>& Out)
    {
        Out.clear();
        if (bInitialized && GEnumerateEntitySystems)
        {
            GEnumerateEntitySystems(reinterpret_cast<void*>(&LmSystemDescSink), &Out);
        }
    }

    void* CreateManagedSystem(FStringView TypeName, uint64 World)
    {
        if (!bInitialized || GCreateEntitySystem == nullptr)
        {
            return nullptr;
        }
        return GCreateEntitySystem(TypeName.data(), (int32)TypeName.size(), World);
    }

    void DestroyManagedSystem(void* Handle)
    {
        if (bInitialized && GDestroyEntitySystem && Handle)
        {
            GDestroyEntitySystem(Handle);
        }
    }

    void TickManagedSystem(void* Handle, const FSystemContext* Context)
    {
        if (bInitialized && GTickEntitySystem && Handle)
        {
            GTickEntitySystem(Handle, const_cast<void*>(reinterpret_cast<const void*>(Context)));
        }
    }

    void DispatchScriptCollision(void* Instance, int32 Kind, const void* Event)
    {
        if (bInitialized && GDispatchCollision && Instance)
        {
            GDispatchCollision(Instance, Kind, Event);
        }
    }

    void DispatchScriptInput(void* Instance, int32 Type, int32 KeyCode, int32 bMouse, int32 Mods, int32 bRepeat,
        double MouseX, double MouseY, double DeltaX, double DeltaY, double Scroll)
    {
        if (bInitialized && GDispatchInput && Instance)
        {
            GDispatchInput(Instance, Type, KeyCode, bMouse, Mods, bRepeat, MouseX, MouseY, DeltaX, DeltaY, Scroll);
        }
    }

    int32 GetScriptCallbackFlags(void* Instance)
    {
        return (bInitialized && GGetCallbackFlags && Instance) ? GGetCallbackFlags(Instance) : 0;
    }

    namespace
    {
        // Captures the schema blob managed writes (Ctx is a TVector<uint8>).
        void LmSchemaBlobSink(void* Ctx, const char* Data, int Len)
        {
            auto* Out = static_cast<TVector<uint8>*>(Ctx);
            if (Out != nullptr && Data != nullptr && Len > 0)
            {
                Out->assign(reinterpret_cast<const uint8*>(Data), reinterpret_cast<const uint8*>(Data) + Len);
            }
        }

        // Little-endian cursor over the managed-written schema blob (see ScriptProperties.BuildSchemaBlob).
        struct FBlobReader
        {
            const uint8* P;
            const uint8* End;

            bool Take(void* Dst, size_t N) { if (P + N > End) { return false; } memcpy(Dst, P, N); P += N; return true; }
            int32   I32() { int32 V = 0; Take(&V, 4); return V; }
            int64   I64() { int64 V = 0; Take(&V, 8); return V; }
            double  F64() { double V = 0; Take(&V, 8); return V; }
            float   F32() { float V = 0; Take(&V, 4); return V; }
            uint8   U8()  { uint8 V = 0; Take(&V, 1); return V; }
            FString Str() { int32 N = I32(); if (N <= 0 || P + N > End) { return FString(); } FString S(reinterpret_cast<const char*>(P), static_cast<size_t>(N)); P += N; return S; }
        };

        FString NumberToString(double V) { char Buf[32]; snprintf(Buf, sizeof(Buf), "%g", V); return FString(Buf); }

        // Append-cursor over a byte vector; mirror of the managed FBlobReader (little-endian).
        struct FBlobWriter
        {
            TVector<uint8>& B;
            void Raw(const void* P, size_t N) { B.insert(B.end(), (const uint8*)P, (const uint8*)P + N); }
            void U8(uint8 V) { B.push_back(V); }
            void I32(int32 V) { Raw(&V, 4); }
            void I64(int64 V) { Raw(&V, 8); }
            void F64(double V) { Raw(&V, 8); }
            void F32(float V) { Raw(&V, 4); }
            void Str(FStringView S) { I32((int32)S.size()); Raw(S.data(), S.size()); }
        };

        // Recursive schema TYPE reader (managed Serializer.WriteType): Array carries its element type,
        // NestedStruct carries named field types. Already nesting-complete on the FScriptExportType side.
        TSharedPtr<Scripting::FScriptExportType> ReadType(FBlobReader& R)
        {
            auto Type = MakeShared<Scripting::FScriptExportType>();
            Type->Kind = static_cast<Scripting::EScriptExportKind>(R.U8());
            if (Type->Kind == Scripting::EScriptExportKind::Array)
            {
                Type->ElementType = ReadType(R);
            }
            else if (Type->Kind == Scripting::EScriptExportKind::NestedStruct)
            {
                const int32 N = R.I32();
                for (int32 i = 0; i < N; ++i)
                {
                    Scripting::FScriptExportField F;
                    F.Name = FName(R.Str().c_str());
                    F.Type = ReadType(R);
                    Type->Fields.push_back(F);
                }
            }
            return Type;
        }

        // Recursive self-describing VALUE reader (managed Serializer.WriteValue). Each value leads with
        // its kind byte so it round-trips without the schema.
        void ReadValue(FBlobReader& R, Scripting::FScriptPropertyValue& Out)
        {
            Out = Scripting::FScriptPropertyValue{};
            Out.Kind = static_cast<Scripting::EScriptExportKind>(R.U8());
            switch (Out.Kind)
            {
                case Scripting::EScriptExportKind::Bool:   Out.AsBool = R.U8() != 0; break;
                case Scripting::EScriptExportKind::Int:    Out.AsInt = R.I64(); break;
                case Scripting::EScriptExportKind::Double: Out.AsDouble = R.F64(); break;
                case Scripting::EScriptExportKind::String: Out.AsString = R.Str(); break;
                case Scripting::EScriptExportKind::Vec2:   Out.AsVec.x = R.F32(); Out.AsVec.y = R.F32(); break;
                case Scripting::EScriptExportKind::Vec3:   Out.AsVec.x = R.F32(); Out.AsVec.y = R.F32(); Out.AsVec.z = R.F32(); break;
                case Scripting::EScriptExportKind::Vec4:   Out.AsVec.x = R.F32(); Out.AsVec.y = R.F32(); Out.AsVec.z = R.F32(); Out.AsVec.w = R.F32(); break;
                case Scripting::EScriptExportKind::Array:
                {
                    const int32 N = R.I32();
                    Out.Items.reserve(N > 0 ? N : 0);
                    for (int32 i = 0; i < N; ++i)
                    {
                        Scripting::FScriptPropertyValue E;
                        ReadValue(R, E);
                        Out.Items.push_back(E);
                    }
                    break;
                }
                case Scripting::EScriptExportKind::NestedStruct:
                {
                    const int32 N = R.I32();
                    Out.StructFields.reserve(N > 0 ? N : 0);
                    for (int32 i = 0; i < N; ++i)
                    {
                        Scripting::FScriptPropertyEntry E;
                        E.Name = FName(R.Str().c_str());
                        ReadValue(R, E.Value);
                        Out.StructFields.push_back(E);
                    }
                    break;
                }
                default: break;
            }
        }

        // Recursive self-describing VALUE writer (mirror of ReadValue); used to ship overrides to managed.
        void WriteValue(FBlobWriter& W, const Scripting::FScriptPropertyValue& V)
        {
            W.U8((uint8)V.Kind);
            switch (V.Kind)
            {
                case Scripting::EScriptExportKind::Bool:   W.U8(V.AsBool ? 1 : 0); break;
                case Scripting::EScriptExportKind::Int:    W.I64(V.AsInt); break;
                case Scripting::EScriptExportKind::Double: W.F64(V.AsDouble); break;
                case Scripting::EScriptExportKind::String: W.Str(FStringView(V.AsString.c_str(), V.AsString.size())); break;
                case Scripting::EScriptExportKind::Vec2:   W.F32(V.AsVec.x); W.F32(V.AsVec.y); break;
                case Scripting::EScriptExportKind::Vec3:   W.F32(V.AsVec.x); W.F32(V.AsVec.y); W.F32(V.AsVec.z); break;
                case Scripting::EScriptExportKind::Vec4:   W.F32(V.AsVec.x); W.F32(V.AsVec.y); W.F32(V.AsVec.z); W.F32(V.AsVec.w); break;
                case Scripting::EScriptExportKind::Array:
                {
                    W.I32((int32)V.Items.size());
                    for (const Scripting::FScriptPropertyValue& E : V.Items)
                    {
                        WriteValue(W, E);
                    }
                    break;
                }
                case Scripting::EScriptExportKind::NestedStruct:
                {
                    W.I32((int32)V.StructFields.size());
                    for (const Scripting::FScriptPropertyEntry& E : V.StructFields)
                    {
                        W.Str(FStringView(E.Name.c_str()));
                        WriteValue(W, E.Value);
                    }
                    break;
                }
                default: break;
            }
        }
        
        // Captures a sink-delivered blob into a TVector<uint8> (the invoke / field-get return path).
        void LmInvokeResultSink(void* Ctx, const char* Data, int Len)
        {
            auto* Out = static_cast<TVector<uint8>*>(Ctx);
            if (Out != nullptr && Data != nullptr && Len > 0)
            {
                Out->assign(reinterpret_cast<const uint8*>(Data), reinterpret_cast<const uint8*>(Data) + Len);
            }
        }

        // One self-describing value (kind byte + payload), wire-compatible with the managed ReadBoxed.
        void WriteManagedArg(FBlobWriter& W, const FManagedValue& V)
        {
            using K = Scripting::EScriptExportKind;
            switch (V.GetKind())
            {
                case EManagedValueKind::Bool:   W.U8((uint8)K::Bool);   W.U8(V.AsBool() ? 1 : 0); break;
                case EManagedValueKind::Int:    W.U8((uint8)K::Int);    W.I64(V.AsInt64()); break;
                case EManagedValueKind::Double: W.U8((uint8)K::Double); W.F64(V.AsDouble()); break;
                case EManagedValueKind::String:
                {
                    const FString& S = V.AsString();
                    W.U8((uint8)K::String);
                    W.Str(FStringView(S.c_str(), S.size()));
                    break;
                }
                default: W.U8((uint8)K::Nil); break;
            }
        }

        // Mirror of WriteManagedArg / the managed WriteBoxed: decode one self-describing value.
        FManagedValue ReadManagedArg(FBlobReader& R)
        {
            using K = Scripting::EScriptExportKind;
            switch ((K)R.U8())
            {
                case K::Bool:   return FManagedValue(R.U8() != 0);
                case K::Int:    return FManagedValue(R.I64());
                case K::Double: return FManagedValue(R.F64());
                case K::String: return FManagedValue(R.Str());
                default:        return FManagedValue();
            }
        }

        // Shared instance/static invoke: packs args, calls the managed entry, decodes the boxed return.
        FManagedValue InvokeManaged(void* Target, bool bStatic, FStringView Method, std::initializer_list<FManagedValue> Args)
        {
            if (!bInitialized || GManagedInvoke == nullptr || Target == nullptr)
            {
                return FManagedValue();
            }

            TVector<uint8> ArgBlob;
            FBlobWriter W{ ArgBlob };
            W.I32((int32)Args.size());
            for (const FManagedValue& A : Args)
            {
                WriteManagedArg(W, A);
            }

            TVector<uint8> RetBlob;
            const int32 Rc = GManagedInvoke(Target, bStatic ? 1 : 0, Method.data(), (int32)Method.size(),
                ArgBlob.data(), (int32)ArgBlob.size(), reinterpret_cast<void*>(&LmInvokeResultSink), &RetBlob);
            if (Rc != 0 || RetBlob.empty())
            {
                return FManagedValue();
            }

            FBlobReader R{ RetBlob.data(), RetBlob.data() + RetBlob.size() };
            return ReadManagedArg(R);
        }
    }
    
    
    FManagedClass::FManagedClass(FStringView TypeName)
    {
        if (bInitialized && GManagedClassFind != nullptr && !TypeName.empty())
        {
            TypeHandle = GManagedClassFind(TypeName.data(), (int32)TypeName.size());
        }
    }

    FManagedClass::~FManagedClass()
    {
        if (TypeHandle != nullptr && GManagedFreeHandle != nullptr)
        {
            GManagedFreeHandle(TypeHandle);
        }
        TypeHandle = nullptr;
    }

    FManagedClass& FManagedClass::operator=(FManagedClass&& Other) noexcept
    {
        if (this != &Other)
        {
            if (TypeHandle != nullptr && GManagedFreeHandle != nullptr)
            {
                GManagedFreeHandle(TypeHandle);
            }
            TypeHandle = Other.TypeHandle;
            Other.TypeHandle = nullptr;
        }
        return *this;
    }

    FManagedObject FManagedClass::New()
    {
        if (TypeHandle == nullptr || GManagedObjectNew == nullptr)
        {
            return FManagedObject();
        }
        return FManagedObject(GManagedObjectNew(TypeHandle));
    }

    FManagedValue FManagedClass::InvokeStatic(FStringView Method, std::initializer_list<FManagedValue> Args)
    {
        return InvokeManaged(TypeHandle, true, Method, Args);
    }

    FManagedObject::~FManagedObject()
    {
        Free();
    }

    void FManagedObject::Free()
    {
        if (Handle != nullptr && GManagedFreeHandle != nullptr)
        {
            GManagedFreeHandle(Handle);
        }
        Handle = nullptr;
    }

    FManagedObject& FManagedObject::operator=(FManagedObject&& Other) noexcept
    {
        if (this != &Other)
        {
            Free();
            Handle = Other.Handle;
            Other.Handle = nullptr;
        }
        return *this;
    }

    FManagedValue FManagedObject::Invoke(FStringView Method, std::initializer_list<FManagedValue> Args)
    {
        return InvokeManaged(Handle, false, Method, Args);
    }

    FManagedValue FManagedObject::GetField(FStringView Name)
    {
        if (!bInitialized || GManagedFieldGet == nullptr || Handle == nullptr)
        {
            return FManagedValue();
        }

        TVector<uint8> RetBlob;
        const int32 Rc = GManagedFieldGet(Handle, Name.data(), (int32)Name.size(),
            reinterpret_cast<void*>(&LmInvokeResultSink), &RetBlob);
        if (Rc != 0 || RetBlob.empty())
        {
            return FManagedValue();
        }

        FBlobReader R{ RetBlob.data(), RetBlob.data() + RetBlob.size() };
        return ReadManagedArg(R);
    }

    bool FManagedObject::SetField(FStringView Name, const FManagedValue& Value)
    {
        if (!bInitialized || GManagedFieldSet == nullptr || Handle == nullptr)
        {
            return false;
        }

        TVector<uint8> ValBlob;
        FBlobWriter W{ ValBlob };
        WriteManagedArg(W, Value);
        return GManagedFieldSet(Handle, Name.data(), (int32)Name.size(), ValBlob.data(), (int32)ValBlob.size()) == 0;
    }

    bool GatherScriptSchema(FStringView ScriptClass, Scripting::FScriptExportSchema& OutSchema, TVector<Scripting::FScriptPropertyEntry>& OutDefaults)
    {
        OutSchema.Fields.clear();
        OutDefaults.clear();
        if (!bInitialized || GGetScriptSchema == nullptr || ScriptClass.empty())
        {
            return false;
        }

        const FString Name(ScriptClass.data(), ScriptClass.size());
        TVector<uint8> Blob;
        GGetScriptSchema(Name.c_str(), (int32)Name.size(), reinterpret_cast<void*>(&LmSchemaBlobSink), &Blob);
        if (Blob.empty())
        {
            return false;
        }

        FBlobReader R{ Blob.data(), Blob.data() + Blob.size() };
        const int32 Count = R.I32();
        for (int32 i = 0; i < Count; ++i)
        {
            Scripting::FScriptExportField Field;
            Field.Name = FName(R.Str().c_str());

            const FString Category = R.Str();
            const FString Tooltip = R.Str();
            const FString Units = R.Str();
            if (!Category.empty())
            {
                Field.Meta.Set("Category", Category);
            }
            if (!Tooltip.empty())
            {
                Field.Meta.Set("Tooltip", Tooltip);
            }
            if (!Units.empty())
            {
                Field.Meta.Set("Units", Units);
            }
            if (R.U8())
            {
                Field.Meta.Set("ClampMin", NumberToString(R.F64()));
            }
            if (R.U8())
            {
                Field.Meta.Set("ClampMax", NumberToString(R.F64()));
            }
            
            const FString AssetType = R.Str();
            if (!AssetType.empty())
            {
                Field.Meta.Set("AssetType", AssetType);
            }
            if (R.U8())
            {
                Field.Meta.Set("Color", FString());
            }
            if (R.U8())
            {
                Field.Meta.Set("Slider", FString());
            }

            Field.Type = ReadType(R);
            Scripting::FScriptPropertyValue Val;
            ReadValue(R, Val);

            OutSchema.Fields.push_back(Field);
            Scripting::FScriptPropertyEntry Entry;
            Entry.Name = Field.Name;
            Entry.Value = Val;
            OutDefaults.push_back(Entry);
        }
        return true;
    }

    void ApplyScriptProperties(void* Instance, const FScriptPropertyOverrides& Overrides)
    {
        if (!bInitialized || GApplyScriptProperties == nullptr || Instance == nullptr)
        {
            return;
        }

        // Serialize the (reconciled) overrides to one recursive value blob and hand it to managed, which
        // deserializes it onto the live instance via reflection. One crossing, nesting-complete.
        TVector<uint8> Blob;
        FBlobWriter W{ Blob };
        W.I32((int32)Overrides.Items.size());
        for (const Scripting::FScriptPropertyEntry& Entry : Overrides.Items)
        {
            W.Str(FStringView(Entry.Name.c_str()));
            WriteValue(W, Entry.Value);
        }
        GApplyScriptProperties(Instance, Blob.data(), (int32)Blob.size());
    }

    bool IsInitialized()
    {
        return bInitialized;
    }

    // Resumes a managed Asset.LoadAsync continuation on the game thread. Reachable from the global
    // extern "C" load export (which can't see the anonymous-namespace delegate directly).
    void DispatchAssetCallback(void* Callback, void* Object)
    {
        if (bInitialized && GInvokeAssetCallback != nullptr)
        {
            GInvokeAssetCallback(Callback, Object);
        }
    }

    namespace
    {
        // Manual reload trigger (the editor's "Reload Scripts" button calls DotNet::ReloadScripts too).
        FAutoConsoleCommand GReloadScriptsCommand(
            "dotnet.reload",
            "Recompile and hot-reload all C# scripts across every mounted Scripts/ folder.",
            []() { Lumina::DotNet::ReloadScripts(); });

        FAutoConsoleCommand GGenProjectsCommand(
            "dotnet.genprojects",
            "(Re)generate the C# IDE project files (.csproj) for every script root.",
            []() { Lumina::DotNet::GenerateScriptProjects(); });
    }
}

// Validates the managed P/Invoke path (LibraryImport "LuminaNative" -> this Runtime module via the
// DllImportResolver). The generated property thunks are exported the same way.
extern "C" RUNTIME_API int LuminaSharp_NativeSelfTest(int A, int B)
{
    return A + B;
}

namespace
{
    Lumina::FEntityRegistry* LmRegistryFromWorld(uint64 World)
    {
        Lumina::CWorld* W = reinterpret_cast<Lumina::CWorld*>(World);
        return W ? &W->GetEntityRegistry() : nullptr;
    }
}

// Resolves a reflected component type name to its op-table token (the C# side caches it per type).
extern "C" RUNTIME_API const void* LuminaSharp_FindComponentOps(const char* Name, int Len)
{
    if (Name == nullptr || Len <= 0)
    {
        return nullptr;
    }
    return Lumina::FindComponentOps(Lumina::FStringView(Name, static_cast<size_t>(Len)));
}

extern "C" RUNTIME_API void* LuminaSharp_GetComponent(uint64 World, uint32 Entity, const void* Ops)
{
    Lumina::FEntityRegistry* R = LmRegistryFromWorld(World);
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    return (R && O) ? O->Get(*R, static_cast<entt::entity>(Entity)) : nullptr;
}

extern "C" RUNTIME_API int LuminaSharp_HasComponent(uint64 World, uint32 Entity, const void* Ops)
{
    Lumina::FEntityRegistry* R = LmRegistryFromWorld(World);
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    return (R && O) ? O->Has(*R, static_cast<entt::entity>(Entity)) : 0;
}

// Get-or-emplace a default component, returning the live pointer to configure in place.
extern "C" RUNTIME_API void* LuminaSharp_EmplaceComponent(uint64 World, uint32 Entity, const void* Ops)
{
    Lumina::FEntityRegistry* R = LmRegistryFromWorld(World);
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    return (R && O) ? O->Emplace(*R, static_cast<entt::entity>(Entity)) : nullptr;
}

extern "C" RUNTIME_API int LuminaSharp_RemoveComponent(uint64 World, uint32 Entity, const void* Ops)
{
    Lumina::FEntityRegistry* R = LmRegistryFromWorld(World);
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    return (R && O) ? O->Remove(*R, static_cast<entt::entity>(Entity)) : 0;
}

// Detached, engine-allocated default instance (the C# `new T()` path); free with DeleteComponent.
extern "C" RUNTIME_API void* LuminaSharp_NewComponent(const void* Ops)
{
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    return O ? O->New() : nullptr;
}

extern "C" RUNTIME_API void LuminaSharp_DeleteComponent(const void* Ops, void* Instance)
{
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    if (O && Instance)
    {
        O->Delete(Instance);
    }
}

// Emplaces a COPY of *Src onto the entity; on the ADD path on_construct fires AFTER the configured
// value is in place. Returns the live component pointer.
extern "C" RUNTIME_API void* LuminaSharp_EmplaceComponentCopy(uint64 World, uint32 Entity, const void* Ops, void* Src)
{
    Lumina::FEntityRegistry* R = LmRegistryFromWorld(World);
    const auto* O = static_cast<const Lumina::FComponentOps*>(Ops);
    return (R && O && Src) ? O->EmplaceCopy(*R, static_cast<entt::entity>(Entity), Src) : nullptr;
}

extern "C" RUNTIME_API void LuminaSharp_SetObjectPtr(void* Member, void* Value)
{
    if (Member != nullptr)
    {
        *reinterpret_cast<Lumina::TObjectPtr<Lumina::CObject>*>(Member) = static_cast<Lumina::CObject*>(Value);
    }
}

// Synchronous (blocking) load by virtual path; returns the CObject* or null. Backs Asset.Load<T>.
extern "C" RUNTIME_API void* LuminaSharp_LoadObject(const char* Path, int Len)
{
    if (Path == nullptr || Len <= 0)
    {
        return nullptr;
    }
    return Lumina::StaticLoadObject(Lumina::FStringView(Path, static_cast<size_t>(Len)));
}

// Registry probe (no load). Backs Asset.Exists.
extern "C" RUNTIME_API int LuminaSharp_AssetExists(const char* Path, int Len)
{
    if (Path == nullptr || Len <= 0)
    {
        return 0;
    }
    return Lumina::FAssetRegistry::Get().GetAssetByPath(Lumina::FStringView(Path, static_cast<size_t>(Len))) != nullptr ? 1 : 0;
}

// Async load; resumes the managed continuation (a GCHandle to an Action) on the game thread with the
// loaded CObject* (or null). Backs Asset.LoadAsync<T>.
extern "C" RUNTIME_API void LuminaSharp_LoadObjectAsync(const char* Path, int Len, void* Callback)
{
    if (Path == nullptr || Len <= 0)
    {
        Lumina::DotNet::DispatchAssetCallback(Callback, nullptr);
        return;
    }

    Lumina::FSoftObjectPath Soft{ Lumina::FStringView(Path, static_cast<size_t>(Len)) };
    Soft.LoadAsync([Callback](Lumina::CObject* Object)
    {
        Lumina::MainThread::Enqueue([Callback, Object]()
        {
            Lumina::DotNet::DispatchAssetCallback(Callback, Object);
        });
    });
}

// Reverse lookup: a loaded CObject* -> its asset's virtual path (registry by GUID). Fills Buffer up to
// Capacity and returns the full length. Backs serializing a C# TObjectPtr<T> field back to a path.
extern "C" RUNTIME_API int LuminaSharp_GetObjectPath(void* Object, char* Buffer, int Capacity)
{
    if (Object == nullptr)
    {
        return 0;
    }
    Lumina::CObject* O = static_cast<Lumina::CObject*>(Object);
    Lumina::FAssetData* Data = Lumina::FAssetRegistry::Get().GetAssetByGUID(O->GetGUID());
    if (Data == nullptr)
    {
        return 0;
    }
    const char* S = Data->Path.c_str();
    const int L = static_cast<int>(Data->Path.size());
    if (Buffer != nullptr && Capacity > 0)
    {
        const int N = L < Capacity ? L : Capacity;
        for (int i = 0; i < N; ++i)
        {
            Buffer[i] = S[i];
        }
    }
    return L;
}

// Maps a module name ("Runtime", "Editor", "Sandbox", ...) to its loaded native handle so the
// managed DllImportResolver can route each generated binding's P/Invoke to the module that exports
// its thunks. Matches the base name up to the build-config suffix ("Editor-Development.dll") or
// extension, so any module's reflected types can carry C# bindings, not just Runtime.
extern "C" RUNTIME_API void* LuminaSharp_ResolveModuleHandle(const char* Name, int Len)
{
    if (Name == nullptr || Len <= 0)
    {
        return nullptr;
    }

#if defined(_WIN32)
    auto Matches = [&](const wchar_t* Base) -> bool
    {
        for (int i = 0; i < Len; ++i)
        {
            const wchar_t Bc = Base[i];
            if (Bc == 0)
            {
                return false; // base name shorter than the queried module name
            }
            const wchar_t A = (Bc >= L'A' && Bc <= L'Z') ? wchar_t(Bc - L'A' + L'a') : Bc;
            const unsigned char Nc = static_cast<unsigned char>(Name[i]);
            const wchar_t B = (Nc >= 'A' && Nc <= 'Z') ? wchar_t(Nc - 'A' + 'a') : wchar_t(Nc);
            if (A != B)
            {
                return false;
            }
        }
        const wchar_t Next = Base[Len]; // must end the stem
        return Next == L'-' || Next == L'.' || Next == 0;
    };

    HANDLE Snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (Snap == INVALID_HANDLE_VALUE)
    {
        return nullptr;
    }

    MODULEENTRY32W Entry;
    Entry.dwSize = sizeof(Entry);
    void* Result = nullptr;
    if (::Module32FirstW(Snap, &Entry))
    {
        do
        {
            if (Matches(Entry.szModule))
            {
                Result = Entry.hModule;
                break;
            }
        }
        while (::Module32NextW(Snap, &Entry));
    }
    ::CloseHandle(Snap);
    return Result;
#else
    struct FCtx { const char* Name; int Len; void* Result; } Ctx{ Name, Len, nullptr };
    ::dl_iterate_phdr([](struct dl_phdr_info* Info, size_t, void* Data) -> int
    {
        FCtx& C = *static_cast<FCtx*>(Data);
        const char* Path = Info->dlpi_name;
        if (Path == nullptr || Path[0] == 0)
        {
            return 0;
        }
        const char* Base = std::strrchr(Path, '/');
        Base = Base ? Base + 1 : Path;
        if (std::strncmp(Base, "lib", 3) == 0)
        {
            Base += 3; // drop the conventional "lib" prefix
        }
        if (std::strncmp(Base, C.Name, static_cast<size_t>(C.Len)) == 0)
        {
            const char Next = Base[C.Len];
            if (Next == '-' || Next == '.' || Next == 0)
            {
                C.Result = ::dlopen(Path, RTLD_NOW | RTLD_NOLOAD);
                return 1;
            }
        }
        return 0;
    }, &Ctx);
    return Ctx.Result;
#endif
}
