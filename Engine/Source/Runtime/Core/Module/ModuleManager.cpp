#include "pch.h"
#include "ModuleManager.h"
#include "ModuleInterface.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Templates/LuminaTemplate.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Platform/Platform.h"
#include "Platform/Process/PlatformProcess.h"


namespace Lumina
{
    FModuleManager& FModuleManager::Get()
    {
        static FModuleManager Instance;
        return Instance;
    }

    IModuleInterface* FModuleManager::LoadModule(FStringView ModulePath)
    {
        // Module names on disk look like "Foo-Development.dll"; the
        // static registry is keyed by bare "Foo". Strip path + ext, then
        // strip a trailing "-<CurrentConfig>" if present.
        // Accept both full DLL paths and already-bare module names (the
        // latter is how plugin manager calls into us in monolithic builds:
        // there's no DLL on disk so it just passes the descriptor name).
        // VFS::FileName returns empty when the input has no slash, so we
        // fall back to the input itself in that case.
        FStringView FileNameView = VFS::FileName(ModulePath, true);
        FString BareName = FileNameView.empty()
            ? FString(ModulePath.data(), ModulePath.size())
            : FString(FileNameView.data(), FileNameView.size());

        // Strip an optional ".dll"/".so" if the caller passed a bare filename
        // with extension (e.g. "Foo.dll") rather than a full path.
        {
            const size_t Dot = BareName.find_last_of('.');
            if (Dot != FString::npos)
            {
                BareName.erase(Dot);
            }
        }

        const FString CfgSuffix = FString("-") + LUMINA_CONFIGURATION_NAME;
        if (BareName.size() > CfgSuffix.size()
            && BareName.substr(BareName.size() - CfgSuffix.size()) == CfgSuffix)
        {
            BareName.erase(BareName.size() - CfgSuffix.size());
        }
        const FName BareFName(BareName);

        // Monolithic / pre-registered path: every module in a monolithic
        // build, plus anything a test fixture might have AddStaticModuleFactory'd,
        // lands here -- no LoadLibrary, no filesystem.
        if (ModuleInitFunc Factory = FindStaticFactory(BareFName))
        {
            IModuleInterface* ModuleInterface = Factory();
            if (!ModuleInterface)
            {
                LOG_WARN("Static module factory returned null: {}", BareName);
                return nullptr;
            }

            FModuleInfo* ModuleInfo = GetOrCreateModuleInfo(BareFName);
            ModuleInfo->ModuleHandle = nullptr;
            ModuleInfo->ModuleInterface.reset(ModuleInterface);

            ModuleInterface->StartupModule();

            LOG_INFO("[Module Manager] - Successfully linked static module {}", BareName);
            FCoreDelegates::OnModuleLoaded.Broadcast(ModuleInfo);
            return ModuleInterface;
        }

        void* ModuleHandle = Platform::GetDLLHandle(StringUtils::ToWideString(ModulePath).c_str());

        if (!ModuleHandle)
        {
            LOG_WARN("Failed to load module: {}", ModulePath);
            return nullptr;
        }

        auto InitFunctionPtr = Platform::LumGetProcAddress<ModuleInitFunc>(ModuleHandle, "InitializeModule");
        if (!InitFunctionPtr)
        {
            LOG_WARN("Failed to get InitializeModule export: {}", ModulePath);
            return nullptr;
        }

        IModuleInterface* ModuleInterface = InitFunctionPtr();

        if (!ModuleInterface)
        {
            LOG_WARN("Module returned null from InitializeModule(): {}", ModulePath);
            return nullptr;
        }

        FStringView ModuleName = VFS::FileName(ModulePath, true);

        FModuleInfo* ModuleInfo = GetOrCreateModuleInfo(ModuleName);
        ModuleInfo->ModuleHandle = ModuleHandle;
        ModuleInfo->ModuleInterface.reset(ModuleInterface);

        ModuleInterface->StartupModule();

        LOG_INFO("[Module Manager] - Successfully loaded module {}", ModuleName);

        FCoreDelegates::OnModuleLoaded.Broadcast(ModuleInfo);

        return ModuleInterface;
    }

    void FModuleManager::AddStaticModuleFactory(const FName& Name, ModuleInitFunc Factory)
    {
        StaticModuleFactories.emplace_back(Name, Factory);
    }

    // Drain the intrusive linked list of pending static registrations into
    // the FName-keyed map. Called lazily by FindStaticFactory on first use
    // -- by then global allocators / FName pool are up.
    static void DrainStaticRegistrationsOnce(FModuleManager& Mgr)
    {
        static bool bDrained = false;
        if (bDrained) return;
        bDrained = true;

        FStaticModuleRegistration* Node = FStaticModuleRegistration::Head;
        while (Node != nullptr)
        {
            Mgr.AddStaticModuleFactory(FName(Node->Name), Node->Factory);
            Node = Node->Next;
        }
    }

    ModuleInitFunc FModuleManager::FindStaticFactory(const FName& Name) const
    {
        DrainStaticRegistrationsOnce(const_cast<FModuleManager&>(*this));
        for (const auto& Entry : StaticModuleFactories)
        {
            if (Entry.first == Name)
            {
                return Entry.second;
            }
        }
        return nullptr;
    }

    // Defined in the header struct as a static. Zero-init in BSS so it's
    // valid before any FStaticModuleRegistration ctor runs.
    FStaticModuleRegistration* FStaticModuleRegistration::Head = nullptr;

    FStaticModuleRegistration::FStaticModuleRegistration(const char* InName, ModuleInitFunc InFactory)
        : Name(InName)
        , Factory(InFactory)
        , Next(Head)
    {
        Head = this;
    }

    bool FModuleManager::UnloadModule(FStringView ModuleName)
    {
        FName ModuleFName = FName(ModuleName);
        auto it = ModuleHashMap.find(ModuleFName);
        
        DEBUG_ASSERT(it != ModuleHashMap.end());

        FModuleInfo& Info = it->second;
        DEBUG_ASSERT(Info.ModuleInterface.get());
        
        Info.ModuleInterface->ShutdownModule();

        ModuleHashMap.erase(it);
        Info.ModuleInterface.reset();
        void* ModulePtr = Info.ModuleHandle;

        // Statically-linked modules have no DLL handle; skip the
        // DLL-side teardown. The IModule's ShutdownModule above already ran.
        if (ModulePtr != nullptr)
        {
            auto ShutdownFunctionPtr = Platform::LumGetProcAddress<ModuleShutdownFunc>(ModulePtr, "ShutdownModule");
            if (ShutdownFunctionPtr)
            {
                ShutdownFunctionPtr();
            }
            Platform::FreeDLLHandle(ModulePtr);
        }

        LOG_INFO("[Module Manager] - Successfully un-loaded module {}", ModuleName);

        return true;
    }

    void FModuleManager::UnloadAllModules()
    {
        TVector<FName> Keys;
        for (const auto& Pair : ModuleHashMap)
        {
            Keys.push_back(Pair.first);
        }

        //@TODO This causes a crash with the heap.
        for (const FName& Key : Keys)
        {
            UnloadModule(Key.ToString());
        }
    }

    FModuleInfo* FModuleManager::GetOrCreateModuleInfo(const FName& ModuleName)
    {
        auto it = ModuleHashMap.find(ModuleName);

        if (it != ModuleHashMap.end())
        {
            return &it->second;
        }

        FModuleInfo NewInfo;
        NewInfo.ModuleName = ModuleName;

        ModuleHashMap.emplace(ModuleName, Move(NewInfo));

        return &ModuleHashMap[ModuleName];
    }

}
