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

    void* Memory::FMalloc::Malloc(size_t Size, size_t Alignment)
    {
        DEBUG_ASSERT(Alignment >= sizeof(void*));
        DEBUG_ASSERT((Alignment & (Alignment - 1)) == 0);
        DEBUG_ASSERT(Alignment % sizeof(void*) == 0);
        
        return rpaligned_alloc(Alignment, Size);
    }

    void* Memory::FMalloc::Realloc(void* Memory, size_t NewSize, size_t Alignment)
    {
        DEBUG_ASSERT(Alignment >= sizeof(void*));
        DEBUG_ASSERT((Alignment & (Alignment - 1)) == 0);
        DEBUG_ASSERT(Alignment % sizeof(void*) == 0);
        
        return rpaligned_realloc(Memory, Alignment, NewSize, 0, 0);
    }

    void Memory::FMalloc::Free(void* Memory)
    {
        rpfree(Memory);
    }

    void Memory::Initialize()
    {
        void* Mem = std::malloc(sizeof(FMalloc));
        new(Mem) FMalloc{};
    }

    void Memory::InitializeThreadHeap()
    {
        if (GMalloc == nullptr)
        {
            Initialize();
        }
        
        rpmalloc_thread_initialize();
    }
    
    void* Memory::Malloc(size_t Size, size_t Alignment)
    {
        void* pMemory = GMalloc->Malloc(Size, Align<size_t>(Alignment, 16));
        LUMINA_PROFILE_ALLOC(pMemory, Size);
        return pMemory;
    }

    void* Memory::Realloc(void* Memory, size_t NewSize, size_t Alignment)
    {
        return GMalloc->Realloc(Memory, NewSize, Align<size_t>(Alignment, 16));
    }

    void Memory::Free(void*& Memory)
    {
        LUMINA_PROFILE_FREE(Memory);
        GMalloc->Free(Memory);
        Memory = nullptr;
    }
}
