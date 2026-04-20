#pragma once
#include "Platform/GenericPlatform.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIFwd.h"


namespace Lumina
{
    class FRenderGraphPass;
    class FRGPassDescriptor;
    enum class ECommandQueue : uint8;
}

namespace Lumina
{
    using FRGPassHandle = FRenderGraphPass*;

    enum class ERGAccess : uint8
    {
        None        = 0,
        Read        = 1 << 0,
        Write       = 1 << 1,
        ReadWrite   = Read | Write,
    };

    ENUM_CLASS_FLAGS(ERGAccess);

    /** Describes a single resource usage by a pass. */
    struct FRGResourceAccess
    {
        IRHIResource*   Resource    = nullptr;
        ERGAccess       Access      = ERGAccess::None;
    };

    /**
     * A group of consecutive passes recorded onto a single command list.
     * All passes in a batch share one state tracker, so resource state carries across them
     * and barriers are emitted with the correct prior-stage source masks.
     * Independent batches (distinct queues, or passes flagged Async) get their own CLs and
     * can be recorded in parallel.
     */
    struct alignas(64) FRGBatch
    {
        ECommandQueue                       Queue = ECommandQueue(0);
        bool                                bIsAsync = false;
        FRHICommandListRef                  CommandList;
        TVector<FRenderGraphPass*>          Passes;
    };

    enum class RUNTIME_API ERGPassFlags : uint8
    {
        None        = 0,

        Raster      = 1 << 0,
        Compute     = 1 << 1,
        Transfer    = 1 << 2,
    };

    ENUM_CLASS_FLAGS(ERGPassFlags);

    #define RG_Raster   ERGPassFlags::Raster
    #define RG_Compute  ERGPassFlags::Compute
    #define RG_Transfer ERGPassFlags::Transfer
}
