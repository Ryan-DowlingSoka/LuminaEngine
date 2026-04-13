#pragma once

#include "PrismDrawElement.h"
#include "PrismTessellator.h"
#include "Renderer/RenderResource.h"

namespace Lumina
{
    class FRHIImage;
    class FRenderGraph;
}

namespace Lumina::Prism
{
    // Prism renderer. Tessellates the per-frame draw list on
    // Submit, then defers GPU work to AddPassToRenderGraph so the host can
    // fold it into whatever render graph they're building for the frame.
    //
    // Buffer management is a simple "grow and stay" scheme: vertex and
    // index buffers are resized when the tessellated stream overflows the
    // current allocation, and are never shrunk. UI batches stabilize very
    // quickly so this converges after the first few frames and then stops
    // touching the allocator entirely.
    class RUNTIME_API FPrismRenderer final
    {
    public:
        FPrismRenderer() = default;
        ~FPrismRenderer() = default;
        LE_NO_COPYMOVE(FPrismRenderer);

        void BeginFrame(const glm::vec2& WindowSize);
        void Submit(const FPrismDrawList& DrawList);
        void EndFrame();

        // Records the Prism draw into the supplied render graph. Target is
        // loaded (not cleared) so Prism composites over whatever the host
        // already rendered into it. Call this after the rest of the frame's
        // passes so UI lands on top.
        void AddPassToRenderGraph(FRenderGraph& RenderGraph, FRHIImage* Target);

        bool HasWork() const { return !VertexStream.Indices.empty(); }

    private:
        void EnsureInputLayout();
        void EnsureBuffers(uint32 VertexCount, uint32 IndexCount);

        FPrismTessellator    Tessellator;
        FPrismVertexStream   VertexStream;
        glm::vec2            CachedWindowSize{0.0f};

        FRHIInputLayoutRef   InputLayout;
        FRHIBufferRef        VertexBuffer;
        FRHIBufferRef        IndexBuffer;
        uint32               VertexCapacity = 0;
        uint32               IndexCapacity  = 0;
    };
}
