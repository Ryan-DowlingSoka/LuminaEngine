#include "pch.h"
#include "Memory.h"

#include "Core/LuminaMacros.h"
#include "Core/Assertions/Assert.h"
#include "Core/Profiler/Profile.h"
#include "Core/Templates/Align.h"

//#undef LUMINA_RPMALLOC

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
    
    namespace
    {
        struct FMalloc
        {
            FMalloc* operator ->() const
            {
                static FMalloc Malloc;
                return &Malloc;
            }
        
            FMalloc() noexcept
            {
                rpmalloc_config_t Config = {};
                Config.error_callback = RPMallocAssert;
                Config.enable_huge_pages = true;
                rpmalloc_initialize_config(&Config);
            }
        
            ~FMalloc()
            {
                rpmalloc_finalize();
            }
        
            LE_NO_COPYMOVE(FMalloc);
        
            void* Malloc(size_t Size, size_t Alignment)
            {
                DEBUG_ASSERT(Alignment >= sizeof(void*));
                DEBUG_ASSERT((Alignment & (Alignment - 1)) == 0);
                DEBUG_ASSERT(Alignment % sizeof(void*) == 0);
            
                return rpaligned_alloc(Alignment, Size);
            }
        
            void* Realloc(void* Memory, size_t NewSize, size_t Alignment)
            {
                DEBUG_ASSERT(Alignment >= sizeof(void*));
                DEBUG_ASSERT((Alignment & (Alignment - 1)) == 0);
                DEBUG_ASSERT(Alignment % sizeof(void*) == 0);
            
                return rpaligned_realloc(Memory, Alignment, NewSize, 0, 0);
            }
        
            void Free(void* Memory)
            {
                rpfree(Memory);
            }
        };
    }
    
    static FMalloc GMalloc;

    void Memory::Initialize()
    {
        // ...
    }

    void Memory::Shutdown()
    {
        //GMalloc->Shutdown();
    }

    void Memory::InitializeThreadHeap()
    {
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
