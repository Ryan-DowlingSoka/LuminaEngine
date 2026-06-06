#pragma once
#include "RenderResource.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{

    class FDescriptorHandle
    {
        friend class FDescriptorTableManager;
    public:

        FDescriptorHandle(const FDescriptorHandle&)             = delete;
        FDescriptorHandle(FDescriptorHandle&&)                  = default;
        FDescriptorHandle& operator=(const FDescriptorHandle&)  = delete;
        FDescriptorHandle& operator=(FDescriptorHandle&&)       = default;

        FDescriptorHandle() = default;
        FDescriptorHandle(const TSharedPtr<FDescriptorTableManager>& InManager, int64 InIndex)
            : Manager(InManager)
            , DescriptorIndex(InIndex)
        {}
        
        ~FDescriptorHandle();

        NODISCARD bool IsValid() const { return DescriptorIndex >= 0 && !Manager.expired(); }
        NODISCARD int64 Get() const 
        { 
            if (DescriptorIndex >= 0)
            {
                ASSERT(!Manager.expired());
            }
            return DescriptorIndex; 
        }

        NODISCARD int64 GetIndexInHeap() const;
        void Reset() { DescriptorIndex = -1; Manager.reset(); }
        
        TWeakPtr<FDescriptorTableManager> Manager;
        int64 DescriptorIndex = -1;
    };
    
    class FDescriptorTableManager : public TSharedFromThis<FDescriptorTableManager>
    {
    public:
        
        FDescriptorTableManager(FRHIBindingLayout* BindingLayout);
        FDescriptorTableManager() = default;
        ~FDescriptorTableManager() = default;
        
        // Custom hasher that doesn't look at the binding slot
        struct FBindingSetItemHasher
        {
            std::size_t operator()(const FBindingSetItem& Item) const
            {
                size_t hash = 0;
                Hash::HashCombine(hash, Item.ResourceHandle);
                Hash::HashCombine(hash, Item.Type);
			
                if (const FBufferRange* Range = Item.TryGetBufferRange())
                {
                    Hash::HashCombine(hash, Range->ByteSize);
                    Hash::HashCombine(hash, Range->ByteOffset);
                }
                else if (const FBindingTextureResource* Texture = Item.TryGetTextureResource())
                {
                    Hash::HashCombine(hash, Texture->Sampler);
                    Hash::HashCombine(hash, Texture->Subresources);
                    Hash::HashCombine(hash, Texture->Dimension);
                }
			
                return hash;
            }
        };

        // Custom equality tester that doesn't look at the binding slot
        struct FBindingSetItemsEqual
        {
            bool operator()(const FBindingSetItem& a, const FBindingSetItem& b) const 
            {
                return a.ResourceHandle == b.ResourceHandle && a.Type == b.Type && a.Variant == b.Variant;
            }
        };

        FRHIDescriptorTable* GetDescriptorTable() const { return DescriptorTable; }

        // Diagnostics: live = currently-allocated descriptors (the index map holds exactly
        // those), capacity = the table's current slot count (grows but never shrinks).
        NODISCARD uint32 GetLiveDescriptorCount() const { return (uint32)DescriptorIndexMap.size(); }
        NODISCARD uint32 GetDescriptorCapacity()  const { return (uint32)AllocatedDescriptors.size(); }

        NODISCARD int64 CreateDescriptor(FBindingSetItem Item);
        NODISCARD FDescriptorHandle CreateDescriptorHandle(const FBindingSetItem& Item);
        NODISCARD const FBindingSetItem& GetDescriptor(int64 Index) const;
        NODISCARD const TVector<FBindingSetItem>& GetDescriptors() const { return Descriptors; }

        // A released slot is NOT returned to the free pool immediately: the GPU may still be
        // sampling it from an in-flight frame (bindless slots are indexed by integer, so the
        // command buffer holds no ref to the texture). The slot is parked until FramesToDefer
        // ticks have passed, then reclaimed. The caller is responsible for repointing the GPU
        // slot at a live placeholder before releasing, so the parked slot never dangles.
        void ReleaseDescriptor(int64 DescriptorIndex);
        void TickDeferredReleases(uint32 FramesToDefer);


    private:

        struct FPendingFree
        {
            int64  Index;
            uint64 FrameReleased;
        };

        FRHIDescriptorTableRef DescriptorTable;
        TVector<FBindingSetItem> Descriptors;
        THashMap<FBindingSetItem, int64, FBindingSetItemHasher, FBindingSetItemsEqual> DescriptorIndexMap;
        TVector<bool> AllocatedDescriptors;
        TVector<FPendingFree> PendingFrees;
        uint64 FrameCounter = 0;
        int64 SearchStart = 0;

    };
}
