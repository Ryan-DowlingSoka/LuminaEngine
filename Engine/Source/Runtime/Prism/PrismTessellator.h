#pragma once

#include "PrismDrawElement.h"
#include "PrismTypes.h"
#include "Containers/Array.h"

namespace Lumina::Prism
{
    class FPrismFontAtlas;

    // Tag identifying the draw style for each emitted vertex, matches the
    // PRIM_* constants in Prism.slang. The pixel shader switches on this to
    // choose between solid fill, SDF outline, texture sample, glyph sample.
    enum class EPrismPrimType : uint32
    {
        Rect   = 0,
        Border = 1,
        Image  = 2,
        Line   = 3,
        Text   = 4,
    };

    // Packed per-vertex record uploaded directly to the GPU. Layout is kept
    // POD with tight float fields so it can flow through Vulkan vertex input
    // without needing bitcasts. Total size: 80 bytes.
    struct FPrismVertex
    {
        glm::vec2 Position;   // Screen-space pixel coordinates.
        glm::vec2 UV;         // Primitive-local UV [0,1] across the quad.
        glm::vec4 Color;      // Premultiplied RGBA tint.
        glm::vec4 ClipRect;   // (MinX, MinY, MaxX, MaxY) scissor in pixels.
        glm::vec2 LocalSize;  // Pixel dimensions of this primitive's quad.
        glm::vec4 Params;     // (CornerRadius, Thickness, Type, TextureIdx)
    };

    // CPU-side staging buffers that the renderer uploads as a single vertex
    // and index pair each frame. The tessellator owns the storage; the
    // renderer copies out when it's time to fill GPU buffers.
    struct FPrismVertexStream
    {
        TVector<FPrismVertex> Vertices;
        TVector<uint32>       Indices;

        void Reset()
        {
            Vertices.clear();
            Indices.clear();
        }

        size_t VertexCount() const { return Vertices.size(); }
        size_t IndexCount()  const { return Indices.size(); }
    };

    // FPrismTessellator turns a FPrismDrawList into a flat, GPU-ready vertex
    // stream. Every draw primitive is expanded into triangles using a quad
    // cover approach: even for borders and lines the quad carries the full
    // primitive footprint, and the pixel shader's SDF decides which pixels
    // are actually shaded. This keeps the vertex pipeline dead simple while
    // still giving us antialiased rounded rectangles and outlines.
    class FPrismTessellator
    {
    public:
        // Flatten InDrawList into OutStream. OutStream is reset first so
        // callers can reuse the same storage across frames. Pass a ready
        // font atlas to get real glyph quads; if null the text primitives
        // fall back to placeholder colored boxes so layouts still compose.
        void Tessellate(const FPrismDrawList& InDrawList, FPrismVertexStream& OutStream, const FPrismFontAtlas* FontAtlas = nullptr) const;

    private:
        static uint32 AppendQuad(FPrismVertexStream& Out,
                                 const glm::vec2& MinPos, const glm::vec2& MaxPos,
                                 const glm::vec2& UV0,    const glm::vec2& UV1,
                                 const glm::vec4& Color,  const glm::vec4& ClipRect,
                                 const glm::vec2& LocalSize,
                                 const glm::vec4& Params);

        void EmitRect    (FPrismVertexStream& Out, const FPrismDrawElement& E) const;
        void EmitBorder  (FPrismVertexStream& Out, const FPrismDrawElement& E) const;
        void EmitImage   (FPrismVertexStream& Out, const FPrismDrawElement& E) const;
        void EmitLine    (FPrismVertexStream& Out, const FPrismDrawElement& E) const;
        void EmitText    (FPrismVertexStream& Out, const FPrismDrawElement& E, const FString& Text, const FPrismFontAtlas* FontAtlas) const;
    };
}
