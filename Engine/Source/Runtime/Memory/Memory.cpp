#include "pch.h"
#include "Memory.h"
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
        return pMemory;
    }

    void* Memory::Realloc(void* Memory, size_t NewSize, size_t Alignment)
    {
        FMalloc& Allocator = (GMalloc != nullptr) ? *GMalloc : EnsureAllocator();
        return Allocator.Realloc(Memory, NewSize, Align<size_t>(Alignment, 16));
    }

    void Memory::Free(void*& Memory)
    {
        LUMINA_PROFILE_FREE(Memory);
        FMalloc& Allocator = (GMalloc != nullptr) ? *GMalloc : EnsureAllocator();
        Allocator.Free(Memory);
        Memory = nullptr;
    }
}
