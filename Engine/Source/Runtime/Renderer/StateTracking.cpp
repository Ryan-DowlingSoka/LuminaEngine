#include "pch.h"
#include "StateTracking.h"

#include <ranges>

#include "RenderResource.h"
#include "Core/Profiler/Profile.h"

namespace Lumina
{
    namespace VkStateTracking
    {
        bool VerifyPermanentResourceState(EResourceStates PermanentState, EResourceStates RequiredState, bool bIsTexture, FStringView DebugName)
        {
            if ((PermanentState & RequiredState) != RequiredState)
            {
                std::ostringstream ossRequired, ossPresent;
                ossRequired << "0x" << std::hex << std::uppercase << uint32(RequiredState);
                ossPresent  << "0x" << std::hex << std::uppercase << uint32(PermanentState);

                LOG_ERROR("Permanent {0} - {1} doesn't have the right state bits. Requires: {2}, Present: {3}",
                    bIsTexture ? "Texture" : "Buffer", DebugName, ossRequired.str(), ossPresent.str());

                return false;
            }

            return true;
        }
    }

    
    namespace
    {
    
        uint32 CalcSubresource(uint32 MipLevel, uint32 ArraySlice, const FRHIImageDesc& Desc)
        {
            return MipLevel + ArraySlice * Desc.NumMips;
        }

    }

    FCommandListResourceStateTracker::FCommandListResourceStateTracker()
        // 64 KiB blocks: must fit the largest single allocation, which is a texture's
        // per-subresource state array (NumMips * ArraySize * sizeof(EResourceStates)).
        : LinearAllocator(64 * 1024)
    {

    }

    void FCommandListResourceStateTracker::SetEnableUavBarriersForTexture(FTextureStateExtension* Texture, bool bEnableBarriers)
    {
        FTextureState* tracking = GetTextureStateTracking(Texture, true);

        tracking->bEnableUavBarriers = bEnableBarriers;
        tracking->bFirstUavBarrierPlaced = false;
    }

    void FCommandListResourceStateTracker::SetEnableUavBarriersForBuffer(FBufferStateExtension* Buffer, bool bEnableBarriers)
    {
        FBufferState* tracking = GetBufferStateTracking(Buffer, true);

        tracking->bEnableUavBarriers = bEnableBarriers;
        tracking->bFirstUavBarrierPlaced = false;
    }

    void FCommandListResourceStateTracker::BeginTrackingTextureState(FTextureStateExtension* texture, FTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        const FRHIImageDesc& desc = texture->DescRef;

        FTextureState* tracking = GetTextureStateTracking(texture, true);
        
        subresources = subresources.Resolve(desc, false);

        if (subresources.IsEntireTexture(desc))
        {
            tracking->State = stateBits;
            tracking->SubresourceStates.clear();
        }
        else
        {
            tracking->SubresourceStates.resize(desc.NumMips * desc.ArraySize, tracking->State);
            tracking->State = EResourceStates::Unknown;

            for (uint32 mipLevel = subresources.BaseMipLevel; mipLevel < subresources.BaseMipLevel + subresources.NumMipLevels; mipLevel++)
            {
                for (uint32 arraySlice = subresources.BaseArraySlice; arraySlice < subresources.BaseArraySlice + subresources.NumArraySlices; arraySlice++)
                {
                    uint32 subresource = CalcSubresource(mipLevel, arraySlice, desc);
                    tracking->SubresourceStates[subresource] = stateBits;
                }
            }
        }
    }

    void FCommandListResourceStateTracker::BeginTrackingBufferState(FBufferStateExtension* buffer, EResourceStates stateBits)
    {
        FBufferState* tracking = GetBufferStateTracking(buffer, true);

        tracking->State = stateBits;
    }

    void FCommandListResourceStateTracker::SetPermanentTextureState(FTextureStateExtension* texture, FTextureSubresourceSet subresources, EResourceStates stateBits)
    {
        const FRHIImageDesc& desc = texture->DescRef;

        subresources = subresources.Resolve(desc, false);

        bool permanent = true;
        if (!subresources.IsEntireTexture(desc))
        {
            LOG_ERROR("Attempted to perform a permanent state transition on a subset of subresources of texture");
            permanent = false;
        }

        RequireTextureState(texture, subresources, stateBits);

        if (permanent)
        {
            PermanentTextureStates.push_back(eastl::make_pair(texture, stateBits));
            GetTextureStateTracking(texture, true)->bPermanentTransition = true;
        }
    }

    void FCommandListResourceStateTracker::SetPermanentBufferState(FBufferStateExtension* buffer, EResourceStates stateBits)
    {
        RequireBufferState(buffer, stateBits);

        PermanentBufferStates.push_back(eastl::make_pair(buffer, stateBits));
    }

    EResourceStates FCommandListResourceStateTracker::GetTextureSubresourceState(FTextureStateExtension* texture, uint32 arraySlice, uint32 mipLevel)
    {
        FTextureState* tracking = GetTextureStateTracking(texture, false);
        if (!tracking)
        {
            return texture->DescRef.bKeepInitialState ? (texture->bStateInitialized ? texture->DescRef.InitialState : EResourceStates::Common) : EResourceStates::Unknown;
        }

        if (tracking->SubresourceStates.empty())
        {
            return tracking->State;
        }

        uint32 subresource = CalcSubresource(mipLevel, arraySlice, texture->DescRef);
        return tracking->SubresourceStates[subresource];
    }

    EResourceStates FCommandListResourceStateTracker::GetBufferState(FBufferStateExtension* buffer)
    {
        FBufferState* tracking = GetBufferStateTracking(buffer, false);

        if (!tracking)
        {
            return EResourceStates::Unknown;
        }

        return tracking->State;
    }

    void FCommandListResourceStateTracker::RequireTextureState(FTextureStateExtension* texture, FTextureSubresourceSet subresources, EResourceStates state)
    {
        if (texture->PermanentState != EResourceStates::Unknown)
        {
            VkStateTracking::VerifyPermanentResourceState(texture->PermanentState, state, true, texture->DescRef.DebugName);
            return;
        }

        subresources = subresources.Resolve(texture->DescRef, false);

        FTextureState* Tracking = GetTextureStateTracking(texture, true);
        
        if (subresources.IsEntireTexture(texture->DescRef) && Tracking->SubresourceStates.empty())
        {
            bool bTransitionNecessary = Tracking->State != state;
            bool bUAVNecessary = ((state & EResourceStates::UnorderedAccess) != EResourceStates::Unknown) && (Tracking->bEnableUavBarriers || !Tracking->bFirstUavBarrierPlaced);

            if (bTransitionNecessary || bUAVNecessary)
            {
                TextureBarriers.emplace_back(FTextureBarrier
                    {
                        .Texture        = texture,
                        .MipLevel       = 0,
                        .ArraySlice     = 0,
                        .NumMipLevels   = 1,
                        .NumArraySlices = 1,
                        .bEntireTexture = true,
                        .StateBefore    = Tracking->State,
                        .StateAfter     = state
                    });
            }

            Tracking->State = state;

            if (bUAVNecessary && !bTransitionNecessary)
            {
                Tracking->bFirstUavBarrierPlaced = true;
            }
        }
        else
        {
            // Expand to per-subresource tracking if currently whole-resource.
            bool bStateExpanded = false;
            if (Tracking->SubresourceStates.empty())
            {
                if (Tracking->State == EResourceStates::Unknown)
                {
                    LOG_ERROR("Unknown prior state of texture {0}. Call CommandList::BeginTrackingTextureState(...) before using the texture or use the KeepInitialState and InitialState members of FRHIImageDesc.",
                        texture->DescRef.DebugName);
                }

                Tracking->SubresourceStates.resize(texture->DescRef.NumMips * texture->DescRef.ArraySize, Tracking->State);
                Tracking->State = EResourceStates::Unknown;
                bStateExpanded = true;
            }

            // Fast path: if all subresources share a prior state, coalesce into one barrier.
            const uint32 BaseArraySlice = subresources.BaseArraySlice;
            const uint32 BaseMipLevel   = subresources.BaseMipLevel;
            const uint32 NumArraySlices = subresources.NumArraySlices;
            const uint32 NumMipLevels   = subresources.NumMipLevels;

            bool bUniformPriorState = true;
            const EResourceStates UniformPriorState = Tracking->SubresourceStates[CalcSubresource(BaseMipLevel, BaseArraySlice, texture->DescRef)];

            for (uint32 ArraySlice = BaseArraySlice; ArraySlice < BaseArraySlice + NumArraySlices && bUniformPriorState; ArraySlice++)
            {
                for (uint32 mipLevel = BaseMipLevel; mipLevel < BaseMipLevel + NumMipLevels; mipLevel++)
                {
                    if (Tracking->SubresourceStates[CalcSubresource(mipLevel, ArraySlice, texture->DescRef)] != UniformPriorState)
                    {
                        bUniformPriorState = false;
                        break;
                    }
                }
            }

            if (bUniformPriorState)
            {
                if (UniformPriorState == EResourceStates::Unknown && !bStateExpanded)
                {
                    LOG_ERROR("Unknown prior state of texture \"{0}\" subresource range (MipLevel = {1}, ArraySlice = {2}). Call CommandList::BeginTrackingTextureState(...) before using the texture or use the keepInitialState and initialState members of TextureDesc.",
                              texture->DescRef.DebugName, BaseMipLevel, BaseArraySlice);
                }

                bool bTransitionNecessary = UniformPriorState != state;
                bool bUAVNecessary = ((state & EResourceStates::UnorderedAccess) != EResourceStates::Unknown) && (Tracking->bEnableUavBarriers || !Tracking->bFirstUavBarrierPlaced);

                if (bTransitionNecessary || bUAVNecessary)
                {
                    TextureBarriers.emplace_back(FTextureBarrier
                    {
                        .Texture        = texture,
                        .MipLevel       = BaseMipLevel,
                        .ArraySlice     = BaseArraySlice,
                        .NumMipLevels   = NumMipLevels,
                        .NumArraySlices = NumArraySlices,
                        .bEntireTexture = false,
                        .StateBefore    = UniformPriorState,
                        .StateAfter     = state
                    });
                }

                for (uint32 ArraySlice = BaseArraySlice; ArraySlice < BaseArraySlice + NumArraySlices; ArraySlice++)
                {
                    for (uint32 mipLevel = BaseMipLevel; mipLevel < BaseMipLevel + NumMipLevels; mipLevel++)
                    {
                        Tracking->SubresourceStates[CalcSubresource(mipLevel, ArraySlice, texture->DescRef)] = state;
                    }
                }

                if (bUAVNecessary && !bTransitionNecessary)
                {
                    Tracking->bFirstUavBarrierPlaced = true;
                }
            }
            else
            {
                // Mixed prior states: per-subresource barriers.
                bool bAnyUavBarrier = false;

                for (uint32 ArraySlice = BaseArraySlice; ArraySlice < BaseArraySlice + NumArraySlices; ArraySlice++)
                {
                    for (uint32 mipLevel = BaseMipLevel; mipLevel < BaseMipLevel + NumMipLevels; mipLevel++)
                    {
                        uint32 subresourceIndex = CalcSubresource(mipLevel, ArraySlice, texture->DescRef);

                        EResourceStates PriorState = Tracking->SubresourceStates[subresourceIndex];

                        if (PriorState == EResourceStates::Unknown && !bStateExpanded)
                        {
                            LOG_ERROR("Unknown prior state of texture \"{0}\" subresource (MipLevel = {1}, ArraySlice = {2}). Call CommandList::BeginTrackingTextureState(...) before using the texture or use the keepInitialState and initialState members of TextureDesc.",
                                      texture->DescRef.DebugName, mipLevel, ArraySlice);
                        }

                        bool bTransitionNecessary = PriorState != state;
                        bool bUAVNecessary = ((state & EResourceStates::UnorderedAccess) != EResourceStates::Unknown) && !bAnyUavBarrier && (Tracking->bEnableUavBarriers || !Tracking->bFirstUavBarrierPlaced);

                        if (bTransitionNecessary || bUAVNecessary)
                        {
                            TextureBarriers.emplace_back(FTextureBarrier
                            {
                                .Texture        = texture,
                                .MipLevel       = mipLevel,
                                .ArraySlice     = ArraySlice,
                                .NumMipLevels   = 1,
                                .NumArraySlices = 1,
                                .bEntireTexture = false,
                                .StateBefore    = PriorState,
                                .StateAfter     = state
                            });
                        }

                        Tracking->SubresourceStates[subresourceIndex] = state;

                        if (bUAVNecessary && !bTransitionNecessary)
                        {
                            bAnyUavBarrier = true;
                            Tracking->bFirstUavBarrierPlaced = true;
                        }
                    }
                }
            }
        }
    }

    void FCommandListResourceStateTracker::RequireBufferState(FBufferStateExtension* buffer, EResourceStates state)
    {
        if (buffer->DescRef.Usage.IsFlagSet(EBufferUsageFlags::Dynamic))
        {
            return;
        }

        if (buffer->PermanentState != EResourceStates::Unknown)
        {
            VkStateTracking::VerifyPermanentResourceState(buffer->PermanentState, state, false, buffer->DescRef.DebugName);
            return;
        }

        if (buffer->DescRef.Usage.IsFlagSet(EBufferUsageFlags::CPUWritable))
        {
            return;
        }

        FBufferState* Tracking = GetBufferStateTracking(buffer, true);

        if (Tracking->State == EResourceStates::Unknown)
        {
            LOG_ERROR("Unknown prior state of buffer \"{0}\". "
                      "Call CommandList::BeginTrackingBufferState(...) before using the buffer or use the "
                      "keepInitialState and initialState members of BufferDesc.",
                      buffer->DescRef.DebugName);
        }

        bool bTransitionNecessary = Tracking->State != state;
        bool bUAVNecessary = ((state & EResourceStates::UnorderedAccess) != EResourceStates::Unknown) && (Tracking->bEnableUavBarriers || !Tracking->bFirstUavBarrierPlaced);

        if (bTransitionNecessary
            && Tracking->BarrierEpoch == CurrentBarrierEpoch
            && Tracking->PendingBarrierIndex >= 0
            && (uint32)Tracking->PendingBarrierIndex < BufferBarriers.size())
        {
            // Combine into the pending barrier (e.g. same buffer used as both index and vertex).
            FBufferBarrier& Existing = BufferBarriers[Tracking->PendingBarrierIndex];
            Existing.StateAfter = EResourceStates(Existing.StateAfter | state);
            Tracking->State = Existing.StateAfter;
            return;
        }

        if (bTransitionNecessary || bUAVNecessary)
        {
            Tracking->PendingBarrierIndex = (int32)BufferBarriers.size();
            Tracking->BarrierEpoch = CurrentBarrierEpoch;
            BufferBarriers.emplace_back(FBufferBarrier
            {
                .Buffer = buffer,
                .StateBefore = Tracking->State,
                .StateAfter = state,
            });
        }

        if (bUAVNecessary && !bTransitionNecessary)
        {
            Tracking->bFirstUavBarrierPlaced = true;
        }

        Tracking->State = state;
    }

    void FCommandListResourceStateTracker::KeepBufferInitialStates()
    {
        for (auto& [buffer, tracking] : BufferStates)
        {
            // Skip when already in the initial state: RequireBufferState would otherwise still emit a
            // UAV barrier (bEnableUavBarriers) for a UAV->UAV "restore" that guards nothing -- there's
            // no work after Close, and next frame's first use re-issues its own hazard barrier.
            if (buffer->DescRef.bKeepInitialState && !buffer->PermanentState && !buffer->DescRef.Usage.IsFlagSet(EBufferUsageFlags::Dynamic) && !tracking->bPermanentTransition
                && tracking->State != buffer->DescRef.InitialState)
            {
                RequireBufferState(buffer, buffer->DescRef.InitialState);
            }
        }
    }

    void FCommandListResourceStateTracker::KeepTextureInitialStates()
    {
        for (auto& [texture, tracking] : TextureStates)
        {
            const bool bEligible = texture->DescRef.bKeepInitialState && !texture->PermanentState && !tracking->bPermanentTransition;
            if (!bEligible)
            {
                continue;
            }

            // Whole-resource and already in the initial state: skip the no-op restore (would still emit
            // a UAV barrier for UAV->UAV). Subresource-tracked textures (State == Unknown) still go
            // through Require, which coalesces/handles them per-range.
            if (tracking->SubresourceStates.empty() && tracking->State == texture->DescRef.InitialState)
            {
                continue;
            }

            RequireTextureState(texture, AllSubresources, texture->DescRef.InitialState);
        }
    }

    void FCommandListResourceStateTracker::CommandListSubmitted()
    {
        for (auto& [texture, state] : PermanentTextureStates)
        {
            const EResourceStates ExistingState = texture->PermanentState;
            if (ExistingState != EResourceStates::Unknown && ExistingState != state)
            {
                LOG_ERROR("Attempted to switch permanent state of texture {0} from 0x{1:X} to 0x{2:X}.",
                          texture->DescRef.DebugName,
                          static_cast<uint32>(ExistingState),
                          static_cast<uint32>(state));
                continue;
            }

            texture->PermanentState = state;
        }

        for (auto& [buffer, state] : PermanentBufferStates)
        {
            const EResourceStates ExistingState = buffer->PermanentState;
            if (ExistingState != EResourceStates::Unknown && ExistingState != state)
            {
                LOG_ERROR("Attempted to switch permanent state of buffer {0} from 0x{1:X} to 0x{2:X}.",
                          buffer->DescRef.DebugName,
                          static_cast<uint32>(ExistingState),
                          static_cast<uint32>(state));
                continue;
            }

            buffer->PermanentState = state;
        }
        
        for (auto& [texture, State] : TextureStates)
        {
            if (texture->DescRef.bKeepInitialState && !texture->bStateInitialized)
            {
                texture->bStateInitialized = true;
            }
        }
        
        PermanentTextureStates.clear();
        PermanentBufferStates.clear();
        TextureStates.clear(); 
        BufferStates.clear();
        LinearAllocator.Reset();
    }

    FTextureState* FCommandListResourceStateTracker::GetTextureStateTracking(FTextureStateExtension* Texture, bool bAllowCreate)
    {
        auto it = TextureStates.find(Texture);

        if (it != TextureStates.end())
        {
            return it->second;
        }

        if (!bAllowCreate)
        {
            return nullptr;
        }

        FTextureState* TrackingRef = LinearAllocator.TAlloc<FTextureState>();
        // Back the (currently empty) subresource array with this tracker's arena so any
        // overflow is bulk-reclaimed on Reset instead of leaking on the heap.
        TrackingRef->SubresourceStates.set_allocator(FFrameArenaAllocator(&LinearAllocator, "TexState"));
        TextureStates.emplace(Texture, TrackingRef);
        
        if (Texture->DescRef.bKeepInitialState)
        {
            TrackingRef->State = Texture->bStateInitialized ? Texture->DescRef.InitialState : EResourceStates::Common;
        }

        return TrackingRef;
    }

    FBufferState* FCommandListResourceStateTracker::GetBufferStateTracking(FBufferStateExtension* Buffer, bool bAllowCreate)
    {
        auto it = BufferStates.find(Buffer);

        if (it != BufferStates.end())
        {
            return it->second;
        }

        if (!bAllowCreate)
        {
            return nullptr;
        }

        FBufferState* TrackingRef = LinearAllocator.TAlloc<FBufferState>();
        BufferStates.emplace(Buffer, TrackingRef);
        
        if (Buffer->DescRef.bKeepInitialState)
        {
            TrackingRef->State = Buffer->DescRef.InitialState;
        }

        return TrackingRef;
    }
}
