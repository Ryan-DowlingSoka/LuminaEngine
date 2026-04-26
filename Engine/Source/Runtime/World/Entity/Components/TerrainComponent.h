#pragma once

#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Renderer/RenderResource.h"
#include "World/Scene/RenderScene/TerrainRenderTypes.h"
#include "TerrainComponent.generated.h"

namespace Lumina
{
    /** One weighted texture layer painted across the terrain surface. */
    REFLECT()
    struct RUNTIME_API STerrainLayer
    {
        GENERATED_BODY()

        /** World-space tiling applied to the layer's albedo/normal sampling. */
        PROPERTY(Editable, Category = "Terrain Layer", ClampMin = 0.01f)
        float UVScale = 1.0f;

        /** Optional human-readable label shown in the paint tool. */
        PROPERTY(Editable, Category = "Terrain Layer")
        FString Name;
    };

    /**
     * Transient GPU-resident state for a terrain instance.
     * Not serialized, rebuilt from Heightmap/LayerWeights on load, and
     * incrementally refreshed when the sculpt/paint tools mark regions dirty.
     */
    struct FTerrainGPUState
    {
        /** R32_FLOAT, Resolution x Resolution, source of truth mirrored from CPU Heightmap. */
        FRHIImageRef    HeightmapTexture;

        /** RG16_SNORM, Resolution x Resolution, XY slope, derived on GPU from heightmap. */
        FRHIImageRef    NormalTexture;

        /** R8_UNORM array, one slice per layer, painted weight maps mirrored from CPU. */
        FRHIImageRef    LayerWeightTexture;

        /** SSBO of FTerrainChunkInfo, one entry per chunk. Cull + render read this. */
        FRHIBufferRef   ChunkInfoBuffer;

        /** SSBO of FTerrainMeshletInfo, one entry per meshlet across all chunks. Cull + render read this. */
        FRHIBufferRef   MeshletInfoBuffer;

        /** SSBO of FTerrainVisibleMeshlet, written by cull, read by render. Sized to total meshlet count. */
        FRHIBufferRef   VisibleMeshletBuffer;

        /** Single FDrawIndirectArguments slot. Cull pass atomic-increments InstanceCount; render reads it. */
        FRHIBufferRef   IndirectDrawBuffer;

        uint32  AllocatedResolution = 0;
        uint32  AllocatedLayerCount = 0;
        uint32  AllocatedChunkCount = 0;
        uint32  AllocatedMeshletCount = 0;

        /** Full reupload scheduled for the next simulate tick (resize / load / clear). */
        bool    bFullHeightmapDirty = true;
        bool    bFullWeightsDirty   = true;

        /** When true, the chunk + meshlet metadata buffers need rebuild before the next cull. */
        bool    bChunksDirty = true;

        /** Inclusive dirty rect on the CPU heightmap awaiting partial upload this frame. */
        glm::ivec2  HeightDirtyMin = glm::ivec2(INT32_MAX);
        glm::ivec2  HeightDirtyMax = glm::ivec2(INT32_MIN);

        /** Inclusive dirty rect per layer awaiting partial upload this frame. */
        glm::ivec2  WeightDirtyMin = glm::ivec2(INT32_MAX);
        glm::ivec2  WeightDirtyMax = glm::ivec2(INT32_MIN);
        uint32      WeightDirtyLayerMask = 0u;

        /** CPU mirror of ChunkInfoBuffer, sized to ChunksPerSide^2. Rebuilt when bChunksDirty. */
        TVector<FTerrainChunkInfo>      Chunks;

        /** CPU mirror of MeshletInfoBuffer, sized to total meshlet count. Rebuilt when bChunksDirty. */
        TVector<FTerrainMeshletInfo>    Meshlets;
    };

    /**
     * Heightmap-driven terrain living entirely on a world entity.
     */
    REFLECT(Component, HideInComponentList)
    struct RUNTIME_API STerrainComponent
    {
        GENERATED_BODY()

        STerrainComponent() = default;

        STerrainComponent(const STerrainComponent& Other)
            : Heightmap(Other.Heightmap)
            , LayerWeights(Other.LayerWeights)
            , Layers(Other.Layers)
            , Material(Other.Material)
            , Resolution(Other.Resolution)
            , ChunkResolution(Other.ChunkResolution)
            , TileWorldSize(Other.TileWorldSize)
            , MaxHeight(Other.MaxHeight)
            , bCastShadow(Other.bCastShadow)
            , bReceiveShadow(Other.bReceiveShadow)
        {
        }

        STerrainComponent& operator=(const STerrainComponent& Other)
        {
            if (this != &Other)
            {
                Heightmap       = Other.Heightmap;
                LayerWeights    = Other.LayerWeights;
                Layers          = Other.Layers;
                Material        = Other.Material;
                Resolution      = Other.Resolution;
                ChunkResolution = Other.ChunkResolution;
                TileWorldSize   = Other.TileWorldSize;
                MaxHeight       = Other.MaxHeight;
                bCastShadow     = Other.bCastShadow;
                bReceiveShadow  = Other.bReceiveShadow;
                GPUState        = FTerrainGPUState{};
            }
            return *this;
        }

        STerrainComponent(STerrainComponent&&) noexcept            = default;
        STerrainComponent& operator=(STerrainComponent&&) noexcept = default;

        /**
         * Height samples packed row-major, size = Resolution * Resolution. Values are
         * normalized displacement in [0, 1] and scaled by MaxHeight on the GPU.
         */
        PROPERTY(Category = "Terrain|Data")
        TVector<float> Heightmap;

        /**
         * Per-layer weight maps packed row-major, size per layer = Resolution * Resolution.
         * Outer index is layer index (matches Layers[]); inner storage is uint8 [0, 255].
         */
        PROPERTY(Category = "Terrain|Data")
        TVector<uint8> LayerWeights;

        /** Paintable material layers blended by LayerWeights. Layer 0 is the base layer. */
        PROPERTY(Editable, Category = "Terrain|Layers")
        TVector<STerrainLayer> Layers;

        /**
         * Master material applied to the terrain. The material's pixel shader drives
         * shading and has access to the per-pixel layer weights via the terrain
         * helpers (GetTerrainLayerWeight / GetTerrainLayerWeights4). When null, the
         * engine's DefaultTerrainMaterial is used at draw time.
         */
        PROPERTY(Editable, Category = "Terrain|Material")
        TObjectPtr<CMaterialInterface> Material;

        /** Heightmap samples per side. Must be (power-of-two + 1) for clean chunking (e.g. 513, 1025). */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 33)
        int32 Resolution = 513;

        /** Samples per chunk side; chunks are the unit of culling and draw dispatch. */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 16)
        int32 ChunkResolution = 64;

        /** Total footprint of the terrain square in world units. */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 1.0f)
        float TileWorldSize = 4096.0f;

        /** World-space displacement applied when Heightmap sample == 1.0. */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 0.0f)
        float MaxHeight = 256.0f;

        /** When true, the terrain writes to shadow maps. */
        PROPERTY(Editable, Category = "Terrain|Shadows")
        bool bCastShadow = true;

        /** When true, the terrain samples shadows cast by other shadow-casting lights. */
        PROPERTY(Editable, Category = "Terrain|Shadows")
        bool bReceiveShadow = true;

        /** Live GPU state populated on the first simulate tick. Not serialized. */
        FTerrainGPUState GPUState;

        /** True when CPU dimensions no longer match GPU allocation, triggers a reallocate. */
        bool NeedsReallocation() const
        {
            const uint32 ExpectedLayers = (uint32)std::max<size_t>(Layers.size(), 1u);
            return GPUState.AllocatedResolution != (uint32)Resolution
                || GPUState.AllocatedLayerCount  != ExpectedLayers;
        }

        /** Mark a rectangular region of the heightmap as dirty for the next upload. */
        void MarkHeightDirty(const glm::ivec2& Min, const glm::ivec2& Max)
        {
            GPUState.HeightDirtyMin = glm::min(GPUState.HeightDirtyMin, Min);
            GPUState.HeightDirtyMax = glm::max(GPUState.HeightDirtyMax, Max);
            // Heights drive chunk/meshlet bounds; flag a rebuild so the cull
            // pass tests against the freshly-sculpted geometry.
            GPUState.bChunksDirty = true;
        }

        /** Mark a rectangular region of a single layer's weights as dirty for the next upload. */
        void MarkWeightsDirty(uint32 LayerIndex, const glm::ivec2& Min, const glm::ivec2& Max)
        {
            GPUState.WeightDirtyMin = glm::min(GPUState.WeightDirtyMin, Min);
            GPUState.WeightDirtyMax = glm::max(GPUState.WeightDirtyMax, Max);
            GPUState.WeightDirtyLayerMask |= (1u << LayerIndex);
        }
    };
}
