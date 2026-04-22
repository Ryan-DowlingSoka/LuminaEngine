#pragma once

#include "RenderTypes.h"
#include "Memory/Allocators/Allocator.h"
#include "Types/BitFlags.h"


namespace Lumina
{
    struct FTextureSubresourceSet;
    struct FRHIImageDesc;
    struct FRHIBufferDesc;
}

namespace Lumina
{
    namespace VkStateTracking
    {
        bool VerifyPermanentResourceState(EResourceStates PermanentState, EResourceStates RequiredState, bool bIsTexture, FStringView DebugName);
    }

    
    struct FBufferStateExtension
    {
        friend class FCommandListResourceStateTracker;

        explicit FBufferStateExtension(const FRHIBufferDesc& desc)
            : DescRef(desc)
        { }

        EResourceStates PermanentState = EResourceStates::Unknown;

    private:
        const FRHIBufferDesc& DescRef;
    };

    struct FTextureStateExtension
    {
        friend class FCommandListResourceStateTracker;
        
        explicit FTextureStateExtension(const FRHIImageDesc& desc)
            : DescRef(desc)
        { }

        EResourceStates PermanentState = EResourceStates::Unknown;
        uint32 bStateInitialized:1 = false;
        uint32 bIsSamplerFeedback:1 = false;
        
    private:
        const FRHIImageDesc& DescRef;
    };

    struct FTextureState
    {
        TFixedVector<EResourceStates, 4> SubresourceStates;
        EResourceStates State = EResourceStates::Unknown;
        uint32 bEnableUavBarriers:1 = true;
        uint32 bFirstUavBarrierPlaced:1 = false;
        uint32 bPermanentTransition:1 = false;
    };

    struct FBufferState
    {
        EResourceStates State = EResourceStates::Unknown;
        // Index of the pending barrier in FCommandListResourceStateTracker::BufferBarriers,
        // tagged with BarrierEpoch. -1 means no pending barrier. Checked against the tracker's
        // CurrentBarrierEpoch so a single bump invalidates all stale indices after a commit.
        int32 PendingBarrierIndex = -1;
        uint32 BarrierEpoch = 0;
        uint32 bEnableUavBarriers:1 = true;
        uint32 bFirstUavBarrierPlaced:1 = false;
        uint32 bPermanentTransition:1 = false;
    };

    struct FTextureBarrier
    {
        FTextureStateExtension* Texture = nullptr;
        uint32 MipLevel = 0;
        uint32 ArraySlice = 0;
        uint32 NumMipLevels = 1;
        uint32 NumArraySlices = 1;
        bool bEntireTexture = false;
        EResourceStates StateBefore = EResourceStates::Unknown;
        EResourceStates StateAfter = EResourceStates::Unknown;
    };

    struct FBufferBarrier
    {
        FBufferStateExtension* Buffer = nullptr;
        EResourceStates StateBefore = EResourceStates::Unknown;
        EResourceStates StateAfter = EResourceStates::Unknown;
    };

    class RUNTIME_API FCommandListResourceStateTracker
    {
    public:

        FCommandListResourceStateTracker();
        
        void SetEnableUavBarriersForTexture(FTextureStateExtension* Texture, bool bEnableBarriers);
        void SetEnableUavBarriersForBuffer(FBufferStateExtension* Buffer, bool bEnableBarriers);

        void BeginTrackingTextureState(FTextureStateExtension* texture, FTextureSubresourceSet subresources, EResourceStates stateBits);
        void BeginTrackingBufferState(FBufferStateExtension* buffer, EResourceStates stateBits);

        void SetPermanentTextureState(FTextureStateExtension* texture, FTextureSubresourceSet subresources, EResourceStates stateBits);
        void SetPermanentBufferState(FBufferStateExtension* buffer, EResourceStates stateBits);

        EResourceStates GetTextureSubresourceState(FTextureStateExtension* texture, uint32 arraySlice, uint32 mipLevel);
        EResourceStates GetBufferState(FBufferStateExtension* buffer);

        // Internal interface
        
        void RequireTextureState(FTextureStateExtension* texture, FTextureSubresourceSet subresources, EResourceStates state);
        void RequireBufferState(FBufferStateExtension* buffer, EResourceStates state);

        void KeepBufferInitialStates();
        void KeepTextureInitialStates();
        void CommandListSubmitted();

        NODISCARD const TFixedVector<FTextureBarrier, 128>& GetTextureBarriers() const { return TextureBarriers; }
        NODISCARD const TFixedVector<FBufferBarrier, 64>& GetBufferBarriers() const { return BufferBarriers; }
        
        void ClearBarriers() { TextureBarriers.clear(); BufferBarriers.clear(); ++CurrentBarrierEpoch; }

    private:
        
        TFixedHashMap<FTextureStateExtension*, FTextureState*, 128> TextureStates;
        TFixedHashMap<FBufferStateExtension*, FBufferState*, 64> BufferStates;

        // Deferred transitions of textures and buffers to permanent states.
        // They are executed only when the command list is executed, not when the app calls setPermanentTextureState or setPermanentBufferState.
        TFixedVector<TPair<FTextureStateExtension*, EResourceStates>, 32> PermanentTextureStates;
        TFixedVector<TPair<FBufferStateExtension*, EResourceStates>, 32> PermanentBufferStates;

        TFixedVector<FTextureBarrier, 128> TextureBarriers;
        TFixedVector<FBufferBarrier, 64> BufferBarriers;

        FTextureState* GetTextureStateTracking(FTextureStateExtension* Texture, bool bAllowCreate);
        FBufferState* GetBufferStateTracking(FBufferStateExtension* Buffer, bool bAllowCreate);

        uint32 CurrentBarrierEpoch = 1;
        FBlockLinearAllocator LinearAllocator;
    };
    
}
