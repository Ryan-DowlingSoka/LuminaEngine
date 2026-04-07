#pragma once
#include <mutex>
#include <shared_mutex>
#include "Platform/GenericPlatform.h"


namespace Lumina
{
    using FThread               = std::thread;
    using FSharedMutex          = std::shared_mutex;
    using FMutex                = std::mutex;
    using FRecursiveMutex       = std::recursive_mutex;
    using FScopeLock            = std::scoped_lock<FMutex>;
    using FReadScopeLock        = std::shared_lock<FSharedMutex>;
    using FWriteScopeLock       = std::unique_lock<FSharedMutex>;
    using FRecursiveScopeLock   = std::scoped_lock<FRecursiveMutex>;

    namespace Threading
    {
        constexpr size_t GCacheLineSize = std::hardware_destructive_interference_size;
        #define ALIGN_FOR_FALSE_SHARING alignas(std::hardware_destructive_interference_size)

        using ThreadID = uint64;

        RUNTIME_API void ThreadYield();
        RUNTIME_API uint64 GetThreadID();
        RUNTIME_API bool IsMainThread();
        RUNTIME_API bool IsRenderThread();

        RUNTIME_API uint32 GetNumThreads();

        RUNTIME_API void SetRenderThread(std::thread::id ID);
        
        RUNTIME_API void Sleep(uint64 Milliseconds);
        
        RUNTIME_API void Initialize(const char* MainThreadName);
        RUNTIME_API void Shutdown();

        RUNTIME_API void InitializeThreadHeap();
        RUNTIME_API void ShutdownThreadHeap();
        RUNTIME_API bool SetThreadName(const char* Name);
    }
    

}
