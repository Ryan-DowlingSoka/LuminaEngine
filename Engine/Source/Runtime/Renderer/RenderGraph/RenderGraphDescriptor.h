#pragma once

#include "RenderGraphTypes.h"
#include "Renderer/RenderResource.h"
#include "Core/LuminaMacros.h"
#include "Renderer/RHIFwd.h"


namespace Lumina
{
    // Async opts out of cross-queue waits — caller owns correctness.
    // AsyncCompute/Transfer route to the matching queue; resources need concurrent sharing.
    enum class ERGExecutionFlags : uint8
    {
        None            = 0,
        Async           = 1 << 0,
        AsyncCompute    = 1 << 1,
        AsyncTransfer   = 1 << 2,
        NeverCull       = 1 << 3,
    };

    ENUM_CLASS_FLAGS(ERGExecutionFlags);

    // Optional Read/Write/ReadWrite declarations enable DAG analysis; otherwise per-queue submission order is used.
    class FRGPassDescriptor
    {
        friend class FRenderGraph;
        friend class FRenderGraphPass;

    public:

        FRGPassDescriptor() = default;

        FRGPassDescriptor& SetFlag(ERGExecutionFlags Flag) { EnumAddFlags(ExecutionFlags, Flag); return *this; }
        bool HasAnyFlag(ERGExecutionFlags Flag) const { return EnumHasAnyFlags(ExecutionFlags, Flag); }
        bool HasAllFlags(ERGExecutionFlags Flags) const { return EnumHasAllFlags(ExecutionFlags, Flags); }

        FRGPassDescriptor& Read(FRHIImage* Image)           { AddAccess(Image,  ERGAccess::Read); return *this; }
        FRGPassDescriptor& Read(FRHIBuffer* Buffer)         { AddAccess(Buffer, ERGAccess::Read); return *this; }
        FRGPassDescriptor& Write(FRHIImage* Image)          { AddAccess(Image,  ERGAccess::Write); return *this; }
        FRGPassDescriptor& Write(FRHIBuffer* Buffer)        { AddAccess(Buffer, ERGAccess::Write); return *this; }
        FRGPassDescriptor& ReadWrite(FRHIImage* Image)      { AddAccess(Image,  ERGAccess::ReadWrite); return *this; }
        FRGPassDescriptor& ReadWrite(FRHIBuffer* Buffer)    { AddAccess(Buffer, ERGAccess::ReadWrite); return *this; }

        const TVector<FRGResourceAccess>& GetAccesses() const { return Accesses; }
        bool HasDeclaredAccesses() const { return !Accesses.empty(); }

    private:

        void AddAccess(IRHIResource* Resource, ERGAccess Access)
        {
            if (Resource == nullptr)
            {
                return;
            }

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
