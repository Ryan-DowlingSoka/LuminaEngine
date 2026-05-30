#pragma once

#include "ModuleInterface.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/Memory.h"
#include "Memory/SmartPtr.h"


// IMPLEMENT_MODULE has two flavors (LUMINA_MONOLITHIC): modular exports InitializeModule for
// GetProcAddress; monolithic registers an intrusive FStaticModuleRegistration drained on lookup.
#ifdef LUMINA_MONOLITHIC

#define IMPLEMENT_MODULE(ModuleClass, ModuleName)                                           \
    namespace {                                                                             \
        Lumina::IModuleInterface* Z_LuminaStaticInit()                                      \
        {                                                                                   \
            return Lumina::Memory::New<ModuleClass>();                                      \
        }                                                                                   \
        const Lumina::FStaticModuleRegistration                                             \
            Z_LuminaStaticReg(ModuleName, &Z_LuminaStaticInit);                             \
    }

#else

#define IMPLEMENT_MODULE(ModuleClass, ModuleName)                                           \
    DECLARE_MODULE_ALLOCATOR_OVERRIDES()                                                    \
    extern "C" __declspec(dllexport) Lumina::IModuleInterface* InitializeModule()           \
    {                                                                                       \
        Lumina::Memory::InitializeThreadHeap();                                             \
        return Lumina::Memory::New<ModuleClass>();                                          \
    }                                                                                       \
    extern "C" __declspec(dllexport) void ShutdownModule()                                  \
    {                                                                                       \
        Lumina::Memory::ShutdownThreadHeap();                                               \
    }

#endif


namespace Lumina
{
    struct RUNTIME_API FModuleInfo
    {
        FName ModuleName;
        TUniquePtr<IModuleInterface> ModuleInterface;
        // null in monolithic builds where the module is statically linked.
        // UnloadModule must guard FreeDLLHandle on it.
        void* ModuleHandle;
    };

    using ModuleInitFunc = IModuleInterface* (*)();
    using ModuleShutdownFunc = void (*)();

    // Monolithic-build static registration; self-links into an intrusive list during static
    // init. Ctor touches zero runtime state (static-init order across TUs is undefined).
    struct RUNTIME_API FStaticModuleRegistration
    {
        const char*                       Name;
        ModuleInitFunc                    Factory;
        FStaticModuleRegistration*        Next;

        FStaticModuleRegistration(const char* InName, ModuleInitFunc InFactory);

        // Head of the pending-registration list; lives in BSS (zero-init) so it's
        // valid before any constructor runs.
        static FStaticModuleRegistration* Head;
    };

    class FModuleManager
    {
    public:

        static FModuleManager& Get();

        // Load a module by DLL path; monolithic builds resolve via the static registry
        // (basename minus -<Config> suffix) with no filesystem touch.
        RUNTIME_API IModuleInterface* LoadModule(FStringView ModulePath);
        RUNTIME_API bool UnloadModule(FStringView ModuleName);

        void UnloadAllModules();

        // Called by FStaticModuleRegistration's ctor at file-scope init; safe before
        // FModuleManager::Get() is otherwise touched (Meyer's singleton resolves on first use).
        void AddStaticModuleFactory(const FName& Name, ModuleInitFunc Factory);

        // True if a module with this bare name is statically linked (monolithic builds).
        bool HasStaticFactory(const FName& Name) const { return FindStaticFactory(Name) != nullptr; }


    private:

        FModuleInfo* GetOrCreateModuleInfo(const FName& ModuleName);

        // Static-registry lookup by bare name (no config suffix); nullptr if not registered.
        ModuleInitFunc FindStaticFactory(const FName& Name) const;


    private:

        THashMap<FName, FModuleInfo> ModuleHashMap;

        // Static module factories from IMPLEMENT_MODULE in monolithic builds (linear list).
        TVector<eastl::pair<FName, ModuleInitFunc>> StaticModuleFactories;
    };
}
