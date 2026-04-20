#pragma once

#include "RenderGraphTypes.h"
#include "Renderer/RenderResource.h"
#include "Core/LuminaMacros.h"
#include "Renderer/RHIFwd.h"


namespace Lumina
{
    /**
     * Flags controlling how a pass is scheduled relative to other passes.
     *
     * - None             : Default. Pass runs in declaration order on its queue.
     *                      Cross-queue waits are inserted automatically.
     *
     * - Async            : The pass has no dependencies on preceding passes. The scheduler is
     *                      free to submit it without cross-queue waits. The user takes
     *                      responsibility for correctness (resources must already be in the
     *                      required state or the pass must transition them).
     *
     * - AsyncCompute     : Compute pass may be routed to the compute queue for async overlap
     *                      with graphics work. Requires that any GPU resources touched are
     *                      either VK_SHARING_MODE_CONCURRENT or have a safe ownership pattern.
     *
     * - AsyncTransfer    : Transfer pass may be routed to the transfer queue.
     *
     * - NeverCull        : Pass is always executed even if its outputs are not consumed.
     */
    enum class ERGExecutionFlags : uint16
    {
        None            = 0,
        Async           = 1 << 0,
        AsyncCompute    = 1 << 1,
        AsyncTransfer   = 1 << 2,
        NeverCull       = 1 << 3,
    };

    ENUM_CLASS_FLAGS(ERGExecutionFlags);

    /**
     * Per-pass description used by the render graph during compilation.
     *
     * Callers may optionally declare resource reads and writes via Read()/Write()/ReadWrite().
     * When declared, the graph uses these to build a data-dependency DAG and can reorder and
     * parallelize passes more aggressively. When no accesses are declared, the graph falls
     * back to per-queue submission order as the implicit dependency.
     */
    class FRGPassDescriptor
    {
        friend class FRenderGraph;
        friend class FRenderGraphPass;

    public:

        FRGPassDescriptor() = default;

        // Execution flags.

        FRGPassDescriptor& SetFlag(ERGExecutionFlags Flag) { EnumAddFlags(ExecutionFlags, Flag); return *this; }
        bool HasAnyFlag(ERGExecutionFlags Flag) const { return EnumHasAnyFlags(ExecutionFlags, Flag); }
        bool HasAllFlags(ERGExecutionFlags Flags) const { return EnumHasAllFlags(ExecutionFlags, Flags); }

        // Resource access declarations. Optional but enables dependency analysis.

        FRGPassDescriptor& Read(FRHIImage* Image)           { AddAccess(reinterpret_cast<IRHIResource*>(Image),  ERGAccess::Read); return *this; }
        FRGPassDescriptor& Read(FRHIBuffer* Buffer)         { AddAccess(reinterpret_cast<IRHIResource*>(Buffer), ERGAccess::Read); return *this; }
        FRGPassDescriptor& Write(FRHIImage* Image)          { AddAccess(reinterpret_cast<IRHIResource*>(Image),  ERGAccess::Write); return *this; }
        FRGPassDescriptor& Write(FRHIBuffer* Buffer)        { AddAccess(reinterpret_cast<IRHIResource*>(Buffer), ERGAccess::Write); return *this; }
        FRGPassDescriptor& ReadWrite(FRHIImage* Image)      { AddAccess(reinterpret_cast<IRHIResource*>(Image),  ERGAccess::ReadWrite); return *this; }
        FRGPassDescriptor& ReadWrite(FRHIBuffer* Buffer)    { AddAccess(reinterpret_cast<IRHIResource*>(Buffer), ERGAccess::ReadWrite); return *this; }

        const TVector<FRGResourceAccess>& GetAccesses() const { return Accesses; }
        bool HasDeclaredAccesses() const { return !Accesses.empty(); }

    private:

        void AddAccess(IRHIResource* Resource, ERGAccess Access)
        {
            if (Resource == nullptr)
            {
                return;
            }

            // Merge duplicates.
            for (FRGResourceAccess& Existing : Accesses)
            {
                if (Existing.Resource == Resource)
                {
                    EnumAddFlags(Existing.Access, Access);
                    return;
                }
            }

            Accesses.push_back({ Resource, Access });
        }

        ERGExecutionFlags               ExecutionFlags = ERGExecutionFlags::None;
        TVector<FRGResourceAccess>      Accesses;
    };
}
