#include "pch.h"
#include "DescriptorTableManager.h"

#include "RenderContext.h"
#include "RHIGlobals.h"

namespace Lumina
{
    FDescriptorHandle::~FDescriptorHandle()
    {
        if (DescriptorIndex >= 0)
        {
            if (auto TableManager = Manager.lock())
            {
                TableManager->ReleaseDescriptor(DescriptorIndex);
            }
            DescriptorIndex = -1;
        }
    }

    int64 FDescriptorHandle::GetIndexInHeap() const
    {
        if (DescriptorIndex >= 0)
        {
            ASSERT(!Manager.expired());
            if (auto LockedManager = Manager.lock())
            {
                return LockedManager->GetDescriptorTable()->GetFirstDescriptorIndexInHeap() + DescriptorIndex;
            }
        }
        
        return -1;
    }

    FDescriptorTableManager::FDescriptorTableManager(FRHIBindingLayout* BindingLayout)
    {
        DescriptorTable = GRenderContext->CreateDescriptorTable(BindingLayout);
        size_t Capacity = DescriptorTable->GetCapacity();
        AllocatedDescriptors.resize(Capacity);
        Descriptors.resize(Capacity);
        Memory::Memzero(Descriptors.data(), sizeof(FBindingSetItem) * Capacity);
    }

    int64 FDescriptorTableManager::CreateDescriptor(FBindingSetItem Item)
    {
        const auto& Found = DescriptorIndexMap.find(Item);
        if (Found != DescriptorIndexMap.end())
        {
            return Found->second;
        }

        size_t Capacity = DescriptorTable->GetCapacity();
        bool bFoundFreeSlot = false;
        uint32 Index = 0;
        for (Index = SearchStart; Index < Capacity; Index++)
        {
            if (!AllocatedDescriptors[Index])
            {
                bFoundFreeSlot = true;
                break;
            }
        }

        if (!bFoundFreeSlot)
        {
            uint32 NewCapacity = Math::Max<uint32>(64u, Capacity * 2);
            GRenderContext->ResizeDescriptorTable(DescriptorTable, NewCapacity, true);
            AllocatedDescriptors.resize(NewCapacity);
            Descriptors.resize(NewCapacity);

            Memory::Memzero(&Descriptors[Capacity], sizeof(FBindingSetItem) * (NewCapacity - Capacity));

            Index = Capacity;
            Capacity = NewCapacity;
        }

        Item.Slot = Index;
        SearchStart = Index + 1;
        AllocatedDescriptors[Index] = true;
        Descriptors[Index] = Item;
        DescriptorIndexMap[Item] = Index;
        GRenderContext->WriteDescriptorTable(DescriptorTable, Item);

        return Index;
    }

    FDescriptorHandle FDescriptorTableManager::CreateDescriptorHandle(const FBindingSetItem& Item)
    {
        int64 Index = CreateDescriptor(Item);
        return FDescriptorHandle(shared_from_this(), Index);
    }

    const FBindingSetItem& FDescriptorTableManager::GetDescriptor(int64 Index) const
    {
        ASSERT((size_t)Index <= Descriptors.size());
        return Descriptors[Index];
    }

    void FDescriptorTableManager::ReleaseDescriptor(int64 DescriptorIndex)
    {
        if (DescriptorIndex < 0 || (size_t)DescriptorIndex >= Descriptors.size())
        {
            return;
        }

        // Already released (and still parked); don't double-enqueue.
        if (!AllocatedDescriptors[DescriptorIndex])
        {
            return;
        }

        const auto IndexMapEntry = DescriptorIndexMap.find(Descriptors[DescriptorIndex]);
        if (IndexMapEntry != DescriptorIndexMap.end())
        {
            DescriptorIndexMap.erase(IndexMapEntry);
        }

        // Drop the CPU record but keep the slot marked allocated so CreateDescriptor won't hand
        // it out. The GPU descriptor is intentionally left untouched here -- the caller has
        // repointed it at a live placeholder, so it stays valid while parked. The slot returns
        // to the free pool only once in-flight frames can no longer reference it.
        Descriptors[DescriptorIndex] = FBindingSetItem();
        PendingFrees.push_back({ DescriptorIndex, FrameCounter });
    }

    void FDescriptorTableManager::TickDeferredReleases(uint32 FramesToDefer)
    {
        ++FrameCounter;

        for (size_t i = 0; i < PendingFrees.size(); )
        {
            if (FrameCounter - PendingFrees[i].FrameReleased >= FramesToDefer)
            {
                const int64 Index = PendingFrees[i].Index;
                AllocatedDescriptors[Index] = false;
                SearchStart = Math::Min<int64>(SearchStart, Index);

                PendingFrees[i] = PendingFrees.back();
                PendingFrees.pop_back();
            }
            else
            {
                ++i;
            }
        }
    }
}
