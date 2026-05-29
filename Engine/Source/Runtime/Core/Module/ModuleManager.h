#pragma once

#include "ModuleInterface.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/Memory.h"
#include "Memory/SmartPtr.h"


// IMPLEMENT_MODULE has two flavors driven by LUMINA_MONOLITHIC (set in
// Shipping config). Modular builds export `InitializeModule` for the
// DLL loader to find via GetProcAddress; monolithic builds drop a
// FStaticModuleRegistration into an intrusive list that FModuleManager
// drains on first lookup. Anonymous namespace per TU keeps the bookkeeping
// names from colliding and dodges token-paste (which breaks on
// namespace-qualified class arguments like Lumina::FFoo).
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

    // File-scope-static helper IMPLEMENT_MODULE emits in monolithic builds.
    // Self-links into an intrusive list during static init. We deliberately
    // touch zero runtime state in the ctor -- no FName, no Memory::New, no
    // FModuleManager::Get() -- because static init order across TUs is
    // undefined and any one of those would crash if its own statics weren't
    // up yet. FModuleManager drains the list on first LoadModule call.
    struct RUNTIME_API FStaticModuleRegistration
    {
        const char*                       Name;
        ModuleInitFunc                    Factory;
        FStaticModuleRegistration*        Next;

        FStaticModuleRegistration(const char* InName, ModuleInitFunc InFactory);

        // Head of the singly-linked list of pending registrations. Pointer
        // lives in BSS (zero-init by the loader) so it's valid before any
        // constructor runs.
        static FStaticModuleRegistration* Head;
    };

    class FModuleManager
    {
    public:

        static FModuleManager& Get();

        // Load a module by DLL path. In modular builds this is the
        // classic LoadLibrary path; in monolithic builds the path's
        // basename (minus the -<Config> suffix) is looked up in the
        // static registry first and instantiated via its factory if
        // found, with no filesystem touch.
        RUNTIME_API IModuleInterface* LoadModule(FStringView ModulePath);
        RUNTIME_API bool UnloadModule(FStringView ModuleName);

        void UnloadAllModules();

        // Called by FStaticModuleRegistration's ctor at file-scope init.
        // Safe to call before FModuleManager::Get() has otherwise been
        // touched -- the Meyer's-singleton resolves on first use.
        void AddStaticModuleFactory(const FName& Name, ModuleInitFunc Factory);

        // True if a module with this bare name is statically linked (as
        // in monolithic Shipping builds). Callers can use this to skip
        // DLL-existence pre-checks before LoadModule.
        bool HasStaticFactory(const FName& Name) const { return FindStaticFactory(Name) != nullptr; }


    private:

        FModuleInfo* GetOrCreateModuleInfo(const FName& ModuleName);

        // Lookup helper for the static registry. Returns nullptr if no
        // module with the bare name (no config suffix) is registered.
        ModuleInitFunc FindStaticFactory(const FName& Name) const;


    private:

        THashMap<FName, FModuleInfo> ModuleHashMap;

        // Static module factories registered by IMPLEMENT_MODULE in
        // monolithic builds. Linear list; population is bounded by
        // the number of modules linked (tens, not thousands), and
        // lookup happens at most N times during engine bring-up.
        TVector<eastl::pair<FName, ModuleInitFunc>> StaticModuleFactories;
    };
}
