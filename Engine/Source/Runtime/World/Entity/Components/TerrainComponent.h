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

    /** Transient GPU state; not serialized, rebuilt on load and on dirty regions. */
    struct FTerrainGPUState
    {
        /** R32_FLOAT mirror of CPU Heightmap. */
        FRHIImageRef    HeightmapTexture;

        /** RG16_SNORM XY slope; derived on GPU. */
        FRHIImageRef    NormalTexture;

        /** R8_UNORM array, one slice per layer. */
        FRHIImageRef    LayerWeightTexture;

        FRHIBufferRef   ChunkInfoBuffer;
        FRHIBufferRef   MeshletInfoBuffer;
        FRHIBufferRef   VisibleMeshletBuffer;

        /** Single FDrawIndirectArguments slot; cull atomic-increments InstanceCount. */
        FRHIBufferRef   IndirectDrawBuffer;

        uint32  AllocatedResolution = 0;
        uint32  AllocatedLayerCount = 0;
        uint32  AllocatedChunkCount = 0;
        uint32  AllocatedMeshletCount = 0;

        bool    bFullHeightmapDirty = true;
        bool    bFullWeightsDirty   = true;

        /** Chunk + meshlet metadata buffers need rebuild before next cull. */
        bool    bChunksDirty = true;

        /** Inclusive dirty rects awaiting partial upload. */
        glm::ivec2  HeightDirtyMin = glm::ivec2(INT32_MAX);
        glm::ivec2  HeightDirtyMax = glm::ivec2(INT32_MIN);

        glm::ivec2  WeightDirtyMin = glm::ivec2(INT32_MAX);
        glm::ivec2  WeightDirtyMax = glm::ivec2(INT32_MIN);
        uint32      WeightDirtyLayerMask = 0u;

        TVector<FTerrainChunkInfo>      Chunks;
        TVector<FTerrainMeshletInfo>    Meshlets;
    };

    /** Heightmap-driven terrain. */
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

        /** Row-major; values normalized [0, 1] scaled by MaxHeight on GPU. */
        PROPERTY(Category = "Terrain|Data")
        TVector<float> Heightmap;

        /** Per-layer weights, row-major uint8 [0, 255]. */
        PROPERTY(Category = "Terrain|Data")
        TVector<uint8> LayerWeights;

        /** Paintable layers; layer 0 is base. */
        PROPERTY(Editable, Category = "Terrain|Layers")
        TVector<STerrainLayer> Layers;

        /** Null = engine DefaultTerrainMaterial at draw time. */
        PROPERTY(Editable, Category = "Terrain|Material")
        TObjectPtr<CMaterialInterface> Material;

        /** Must be (pow2 + 1) for clean chunking (513, 1025). */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 33)
        int32 Resolution = 513;

        /** Cull/draw dispatch unit. */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 16)
        int32 ChunkResolution = 64;

        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 1.0f)
        float TileWorldSize = 4096.0f;

        /** World displacement at heightmap sample == 1.0. */
        PROPERTY(Editable, Category = "Terrain|Layout", ClampMin = 0.0f)
        float MaxHeight = 256.0f;

        PROPERTY(Editable, Category = "Terrain|Shadows")
        bool bCastShadow = true;

        PROPERTY(Editable, Category = "Terrain|Shadows")
        bool bReceiveShadow = true;

        /** Transient; populated on first simulate tick. */
        FTerrainGPUState GPUState;

        bool NeedsReallocation() const
        {
            const uint32 ExpectedLayers = (uint32)std::max<size_t>(Layers.size(), 1u);
            return GPUState.AllocatedResolution != (uint32)Resolution
                || GPUState.AllocatedLayerCount  != ExpectedLayers;
        }

        void MarkHeightDirty(const glm::ivec2& Min, const glm::ivec2& Max)
        {
            GPUState.HeightDirtyMin = glm::min(GPUState.HeightDirtyMin, Min);
            GPUState.HeightDirtyMax = glm::max(GPUState.HeightDirtyMax, Max);
            // Heights drive chunk/meshlet bounds; cull must test against fresh geometry.
            GPUState.bChunksDirty = true;
        }

        void MarkWeightsDirty(uint32 LayerIndex, const glm::ivec2& Min, const glm::ivec2& Max)
        {
            GPUState.WeightDirtyMin = glm::min(GPUState.WeightDirtyMin, Min);
            GPUState.WeightDirtyMax = glm::max(GPUState.WeightDirtyMax, Max);
            GPUState.WeightDirtyLayerMask |= (1u << LayerIndex);
        }
    };
}
