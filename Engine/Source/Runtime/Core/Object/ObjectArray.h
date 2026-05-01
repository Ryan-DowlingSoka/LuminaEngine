#pragma once

#include "ObjectBase.h"
#include "ObjectHandle.h"
#include "Containers/Array.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Thread.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class CObjectBase;
}

namespace Lumina
{
    struct FCObjectEntry
    {
        FCObjectEntry() = default;
        
        CObjectBase* Object = nullptr;
        TAtomic<int32> Generation{0};
        TAtomic<int32> StrongRefCount{0};
        TAtomic<int32> WeakRefCount{0};
        
        // Non-copyable
        FCObjectEntry(FCObjectEntry&&) = delete;
        FCObjectEntry(const FCObjectEntry&) = delete;
        FCObjectEntry& operator=(FCObjectEntry&&) = delete;
        FCObjectEntry& operator=(const FCObjectEntry&) = delete;

        FORCEINLINE CObjectBase* GetObj() const
        {
            return Object;
        }

        FORCEINLINE void SetObj(CObjectBase* InObject)
        {
            Object = InObject;
        }

        void AddStrongRef()
        {
            StrongRefCount.fetch_add(1, std::memory_order_relaxed);
        }

        uint32 ReleaseStrongRef()
        {
            int32 PrevCount = StrongRefCount.load(std::memory_order_relaxed);
            do
            {
                if (PrevCount <= 0)
                {
                    ASSERT(false, "ReleaseStrongRef on object with zero refcount");
                    return 0;
                }
            }
            while (!StrongRefCount.compare_exchange_weak(PrevCount, PrevCount - 1,
                std::memory_order_acq_rel, std::memory_order_relaxed));
            return uint32(PrevCount - 1);
        }

        void AddWeakRef()
        {
            WeakRefCount.fetch_add(1, std::memory_order_relaxed);
        }

        void ReleaseWeakRef()
        {
            WeakRefCount.fetch_sub(1, std::memory_order_relaxed);
        }

        int32 GetStrongRefCount() const
        {
            return StrongRefCount.load(std::memory_order_relaxed);
        }

        int32 GetWeakRefCount() const
        {
            return WeakRefCount.load(std::memory_order_relaxed);
        }

        int32 GetGeneration() const
        {
            return Generation.load(std::memory_order_acquire);
        }

        void IncrementGeneration()
        {
            Generation.fetch_add(1, std::memory_order_release);
        }

        bool IsReferenced() const
        {
            return StrongRefCount.load(std::memory_order_relaxed) > 0;
        }

        void ResetRefCounts()
        {
            StrongRefCount.store(0, std::memory_order_relaxed);
            WeakRefCount.store(0, std::memory_order_relaxed);
        }
    };

    /**
     * Global control block for all CObjects, never reallocates.
     */
    class FChunkedFixedCObjectArray
    {
    public:
        
        static constexpr int32 NumElementsPerChunk = 64 * 1024;
    
    private:
        
        // Array of chunk pointers.
        FCObjectEntry** Objects = nullptr;
    
        int32 MaxElements = 0;
        int32 NumElements = 0;
        int32 MaxChunks = 0;
        int32 NumChunks = 0;
    
        FMutex AllocationMutex;
    
    public:
        
        FChunkedFixedCObjectArray() = default;
        ~FChunkedFixedCObjectArray()
        {
            Shutdown();
        }

        LE_NO_COPYMOVE(FChunkedFixedCObjectArray);

        
        void Initialize(int32 InMaxElements);
        
        void Shutdown();
    
        void PreAllocateAllChunks();
    
        RUNTIME_API const FCObjectEntry* GetItem(int32 Index) const;
    
        RUNTIME_API FCObjectEntry* GetItem(int32 Index);
    
        FORCEINLINE int32 GetMaxElements() const { return MaxElements; }
        FORCEINLINE int32 GetNumElements() const { return NumElements; }
        FORCEINLINE int32 GetNumChunks() const { return NumChunks; }
    
        // Increment element count (called by FCObjectArray on allocation)
        FORCEINLINE void IncrementElementCount()
        {
            ++NumElements;
        }
    
        // Decrement element count (called by FCObjectArray on deallocation)
        FORCEINLINE void DecrementElementCount()
        {
            --NumElements;
        }
    
    };
        
    class FCObjectArray
    {
    private:
        
        FRecursiveMutex             Mutex;
        FChunkedFixedCObjectArray   ChunkedArray;
        TVector<int32>              FreeIndices;
        bool                        bInitialized = false;
        bool                        bShuttingDown = false;
    
    public:
        FCObjectArray() = default;
        ~FCObjectArray() = default;

        LE_NO_COPYMOVE(FCObjectArray);
    
        // Initialize the object array with maximum capacity
        void AllocateObjectPool(int32 InMaxCObjects);
    
        void Shutdown();
    
        // Allocate a slot for an object and return a handle
        FObjectHandle AllocateObject(CObjectBase* Object);
    
        // Deallocate an object slot
        void DeallocateObject(int32 Index);
    
        // Resolve a handle to an object pointer
        
        RUNTIME_API CObjectBase* ResolveHandle(const FObjectHandle& Handle) const;
        
        RUNTIME_API CObjectBase* GetObjectByIndex(int32 Index) const;

        RUNTIME_API FObjectHandle GetHandleByObject(const CObjectBase* Object) const;
        
    
        // Get handle from object index and current generation
        RUNTIME_API FObjectHandle GetHandleByIndex(int32 Index) const;

        // Add a strong reference to an object by pointer
        RUNTIME_API void AddStrongRef(CObjectBase* Object);

        // Release a strong reference by pointer
        // Returns true if object was deleted.
        RUNTIME_API bool ReleaseStrongRef(CObjectBase* Object);
    
        RUNTIME_API void AddStrongRefByIndex(int32 Index);
    
        RUNTIME_API bool ReleaseStrongRefByIndex(int32 Index);

        RUNTIME_API void AddWeakRefByIndex(int32 Index);

        RUNTIME_API void ReleaseWeakRefByIndex(int32 Index);

        RUNTIME_API bool IsReferencedByIndex(int32 Index) const;

        RUNTIME_API int32 GetStrongRefCountByIndex(int32 Index) const;
    
        RUNTIME_API int32 GetNumAliveObjects() const;
    
        RUNTIME_API int32 GetMaxObjects() const;
    
        template<typename Func>
        requires(eastl::is_invocable_v<Func, CObjectBase*, int32>)
        void ForEachObject(Func&& Function) const
        {
            const int32 MaxElements = ChunkedArray.GetNumElements();
            
            for (int32 i = 0; i < MaxElements; ++i)
            {
                const FCObjectEntry* Item = ChunkedArray.GetItem(i);
                if (Item && Item->GetObj())
                {
                    eastl::invoke(Function, Item->GetObj(), i);
                }
            }
        }
    };
    
    // Global object array instance
    extern RUNTIME_API FCObjectArray GObjectArray;
    
}
