#include "pch.h"
#include "Memory.h"
#include "MemoryTracking.h"
#include "Allocators/Allocator.h"
#include "Core/LuminaMacros.h"
#include "Core/Assertions/Assert.h"
#include "Core/Profiler/Profile.h"
#include "Core/Templates/Align.h"

namespace Lumina
{
    static void RPMallocAssert(const char* pMessage)
    {
        if (pMessage && strstr(pMessage, "Memory leak detected"))
        {
            return;
        }
        
        PANIC("{}", pMessage);
    }
    
    RUNTIME_API Memory::FMalloc* Memory::GMalloc = nullptr;

    Memory::FMalloc::FMalloc() noexcept
    {
        rpmalloc_config_t Config = {};
        Config.error_callback = RPMallocAssert;
        Config.enable_huge_pages = true;
        rpmalloc_initialize_config(&Config);
    }

    namespace
    {
        // Magic-static bootstrap so dynamic-init allocations come up cleanly; also publishes GMalloc.
        Memory::FMalloc& EnsureAllocator()
        {
            static Memory::FMalloc Instance;
            Memory::GMalloc = &Instance;
            return Instance;
        }
    }

    void* Memory::FMalloc::Malloc(size_t Size, size_t Alignment)
    {
        DEBUG_ASSERT(Alignment >= sizeof(void*));
        DEBUG_ASSERT((Alignment & (Alignment - 1)) == 0);
        DEBUG_ASSERT(Alignment % sizeof(void*) == 0);
        
        if (!rpmalloc_is_thread_initialized())
        {
            rpmalloc_thread_initialize();
        }

        return rpaligned_alloc(Alignment, Size);
    }

    void* Memory::FMalloc::Realloc(void* Memory, size_t NewSize, size_t Alignment)
    {
        DEBUG_ASSERT(Alignment >= sizeof(void*));
        DEBUG_ASSERT((Alignment & (Alignment - 1)) == 0);
        DEBUG_ASSERT(Alignment % sizeof(void*) == 0);

        if (!rpmalloc_is_thread_initialized())
        {
            rpmalloc_thread_initialize();
        }

        return rpaligned_realloc(Memory, Alignment, NewSize, 0, 0);
    }

    void Memory::FMalloc::Free(void* Memory)
    {
        rpfree(Memory);
    }

    size_t Memory::GetCurrentMappedMemory()   { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.mapped; }
    size_t Memory::GetPeakMappedMemory()       { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.mapped_peak; }
    size_t Memory::GetCachedMemory()           { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.cached; }
    size_t Memory::GetCurrentHugeAllocMemory() { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.huge_alloc; }
    size_t Memory::GetPeakHugeAllocMemory()    { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.huge_alloc_peak; }
    size_t Memory::GetTotalMappedMemory()      { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.mapped_total; }
    size_t Memory::GetTotalUnmappedMemory()    { rpmalloc_global_statistics_t s; rpmalloc_global_statistics(&s); return s.unmapped_total; }

    void Memory::Initialize()
    {
        (void)EnsureAllocator();
    }

    void Memory::InitializeThreadHeap()
    {
        (void)EnsureAllocator();
        rpmalloc_thread_initialize();
    }

    void* Memory::Malloc(size_t Size, size_t Alignment)
    {
        FMalloc& Allocator = (GMalloc != nullptr) ? *GMalloc : EnsureAllocator();
        void* pMemory = Allocator.Malloc(Size, Align<size_t>(Alignment, 16));
        LUMINA_PROFILE_ALLOC(pMemory, Size);
    #if LUMINA_MEMORY_TRACKING
        ::Lumina::Memory::Hooks::OnAlloc(pMemory, Size);
    #endif
        return pMemory;
    }

    void* Memory::Realloc(void* Memory, size_t NewSize, size_t Alignment)
    {
        FMalloc& Allocator = (GMalloc != nullptr) ? *GMalloc : EnsureAllocator();
        void* pMemory = Allocator.Realloc(Memory, NewSize, Align<size_t>(Alignment, 16));
    #if LUMINA_MEMORY_TRACKING
        ::Lumina::Memory::Hooks::OnRealloc(Memory, pMemory, NewSize);
    #endif
        return pMemory;
    }

    void Memory::Free(void*& Memory)
    {
        LUMINA_PROFILE_FREE(Memory);
    #if LUMINA_MEMORY_TRACKING
        ::Lumina::Memory::Hooks::OnFree(Memory);
    #endif
        FMalloc& Allocator = (GMalloc != nullptr) ? *GMalloc : EnsureAllocator();
        Allocator.Free(Memory);
        Memory = nullptr;
    }

    FBlockLinearAllocator& GetThreadScratchAllocator()
    {
        // 256 KB blocks; chains more on demand and reuses them across FMemMark scopes.
        static constexpr SIZE_T ScratchBlockSize = 256 * 1024;
        thread_local FBlockLinearAllocator GScratch(ScratchBlockSize);
        return GScratch;
    }
}

// C-ABI shim exported from Runtime.dll so third-party static libs route through our allocator.
#pragma comment(linker, "/EXPORT:LmThirdPartyMalloc")
#pragma comment(linker, "/EXPORT:LmThirdPartyRealloc")
#pragma comment(linker, "/EXPORT:LmThirdPartyCalloc")
#pragma comment(linker, "/EXPORT:LmThirdPartyFree")

#if LUMINA_MEMORY_TRACKING
    #define LM_TP_SCOPE(Category) \
        ::Lumina::Memory::FMemoryScope LmTpScope(::Lumina::Memory::RegisterCategory((Category) ? (Category) : "ThirdParty"))
#else
    #define LM_TP_SCOPE(Category) ((void)(Category))
#endif

extern "C" void* LmThirdPartyMalloc(size_t Size, const char* Category)
{
    LM_TP_SCOPE(Category);
    return ::Lumina::Memory::Malloc(Size);
}

extern "C" void* LmThirdPartyRealloc(void* Ptr, size_t Size, const char* Category)
{
    LM_TP_SCOPE(Category);
    return ::Lumina::Memory::Realloc(Ptr, Size);
}

extern "C" void* LmThirdPartyCalloc(size_t Count, size_t Size, const char* Category)
{
    const size_t Total = Count * Size;
    if (Count != 0 && Total / Count != Size) // multiply overflow
    {
        return nullptr;
    }

    LM_TP_SCOPE(Category);
    void* Ptr = ::Lumina::Memory::Malloc(Total);
    if (Ptr != nullptr)
    {
        ::Lumina::Memory::Memset(Ptr, 0, Total);
    }
    return Ptr;
}

extern "C" void LmThirdPartyFree(void* Ptr)
{
    if (Ptr != nullptr)
    {
        ::Lumina::Memory::Free(Ptr);
    }
}

#undef LM_TP_SCOPE
