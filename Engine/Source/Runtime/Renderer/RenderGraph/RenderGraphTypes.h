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

    struct FRGResourceAccess
    {
        IRHIResource*   Resource    = nullptr;
        ERGAccess       Access      = ERGAccess::None;
    };

    // Same-queue passes share a CL and state tracker; independent batches record in parallel.
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
