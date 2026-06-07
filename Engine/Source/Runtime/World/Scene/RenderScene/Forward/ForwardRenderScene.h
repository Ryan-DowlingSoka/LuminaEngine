#pragma once
#include <condition_variable>
#include "Core/Delegates/Delegate.h"
#include "Memory/Allocators/Allocator.h"
#include "Memory/SmartPtr.h"
#include "Renderer/BindingCache.h"
#include "Renderer/Vertex.h"
#include "TaskSystem/TaskGraph.h"
#include "World/Entity/Components/LineBatcherComponent.h"
#include "World/Scene/RenderScene/EnvironmentRenderTypes.h"
#include "World/Scene/RenderScene/MeshDrawCommand.h"
#include "World/Scene/RenderScene/RenderScene.h"
#include "World/Scene/RenderScene/SceneCullContext.h"
#include "World/Scene/RenderScene/TerrainRenderTypes.h"
#include "World/Scene/RenderScene/TexturePaintTypes.h"
#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "TaskSystem/FiberSync.h"
#include "World/Entity/Components/PostProcessSettings.h"
#include "World/Subsystems/WorldSettings.h"


namespace Lumina
{
    class CMesh;
    struct FLineBatcherComponent;
    struct FTriangleBatcherComponent;
    struct SDirectionalLightComponent;
    struct SSpotLightComponent;
    struct SPointLightComponent;
    struct SExponentialHeightFogComponent;
    class CWorld;
    struct SStaticMeshComponent;
    struct SSkeletalMeshComponent;
    struct STransformComponent;
    struct STerrainComponent;
    struct SDecalComponent;
    class CMaterialInterface;
    class CMaterial;

    /** Scene rendering via Clustered Forward Rendering. */
    class FForwardRenderScene : public IRenderScene
    {
    public:
        
        FForwardRenderScene(CWorld* InWorld);
        ~FForwardRenderScene() override = default;
        LE_NO_COPYMOVE(FForwardRenderScene);
        
        // Per-entity shared data. FProcessedDrawItem carries an EntityRecordIndex
        // into this table so Transform/Bounds aren't duplicated per surface.
        struct FEntityRecord
        {
            FMatrix4               Transform;
            FVector4               SphereBounds;
            uint64                  MeshletHeaderAddress;
            uint32                  CustomData;
            uint32                  EntityID;
            uint32                  LocalBoneOffset;            // ~0u for static meshes.
            // GPU pre-skinning: only the rendered-LOD meshlet span is skinned (0 for static).
            // SpanStart = first rendered vertex, SliceSize = extent; GlobalSkinnedBase resolved in merge.
            uint32                  SkinMeshletStart;
            uint32                  SkinMeshletCount;
            uint32                  SkinSpanStart;
            uint32                  SkinSliceSize;
            uint32                  GlobalSkinnedBase;
        };

        // Per-(entity, surface) item. Composed into an FGPUInstance during the
        // parallel write phase by reading the shared FEntityRecord.
        struct FProcessedDrawItem
        {
            uint32              EntityRecordIndex;
            uint32              SurfaceMeshletOffset;
            uint32              SurfaceMeshletCount;
            uint32              ShadowMeshletOffset;
            uint32              ShadowMeshletCount;
            EInstanceFlags      Flags;
            uint16              MaterialIndex;
            uint16              LocalBatchIndex;
            uint16              LocalDrawIndex;
            uint16              _Pad;
        };

        // Per-thread local batch table; worker linear-searches it to dedup draws.
        // Merge walks these, making it O(unique batches x threads), not O(instances).
        struct alignas(64) FLocalBatchEntry
        {
            FDrawBatchKey                       Key;
            FRHIVertexShader*                   VertexShader = nullptr;
            FRHIPixelShader*                    PixelShader  = nullptr;
            // Per-material depth-prepass / shadow VS. Null for non-WPO materials
            // -- renderer falls back to the global.
            FRHIVertexShader*                   DepthVertexShader  = nullptr;
            FRHIVertexShader*                   ShadowVertexShader = nullptr;
            TFrameVector<FDrawKey>              LocalDraws;
            TFrameVector<uint32>                LocalDrawCounts;
            TFrameVector<uint32>                LocalMeshletCounts;
            // O(1) lookup into LocalDraws.
            TFrameHashMap<FDrawKey, uint16>     DrawIndexByKey;
            uint32                              GlobalBatchIndex = ~0u;
            TFrameVector<uint32>                LocalToGlobalDraw;
            TFrameVector<uint32>                LocalDrawWriteBase;

            FLocalBatchEntry() = default;
            explicit FLocalBatchEntry(FFrameArenaAllocator A)
                : LocalDraws(A), LocalDrawCounts(A), LocalMeshletCounts(A)
                , DrawIndexByKey(A), LocalToGlobalDraw(A), LocalDrawWriteBase(A) {}
        };

        // Material-pure portion of a resolved draw slot, cached per-thread keyed
        // by material. Per-entity bits (CastShadow / bDrawInDepthPass) added in ResolveSlot.
        struct FCachedMaterialResolve
        {
            FRHIVertexShader*   VertexShader;
            FRHIPixelShader*    PixelShader;
            FRHIVertexShader*   DepthVertexShader;
            FRHIVertexShader*   ShadowVertexShader;
            uint64              MaterialID;
            uint16              MaterialIdx;
            bool                bTranslucent;
            bool                bMasked;
            bool                bAdditive;
            bool                bTwoSided;
            bool                bMaterialCastsShadows;
        };

        struct FMaterialCacheEntry
        {
            CMaterialInterface*     Key;
            FCachedMaterialResolve  Resolve;
        };

        struct alignas(64) FThreadLocalDrawData
        {
            TFrameVector<FProcessedDrawItem>    Items;
            TFrameVector<FEntityRecord>         EntityRecords;
            TFrameVector<FLocalBatchEntry>      LocalBatches;
            // 48B/bone (last row dropped). Arena-backed; arena block must exceed one thread's worst-case bone vector.
            TFrameVector<FBoneTransform>        BonesData;
            TFrameHashMap<CMesh*, uint8>  PinnedMeshDedupe;
            // Heap-backed so refs survive the per-thread arena reset.
            TVector<FRHIBufferRef>              PinnedMeshBuffers;
            // Per-thread material resolve cache; linear-scanned (few unique materials per thread).
            TFrameVector<FMaterialCacheEntry>   MaterialCache;
            // Fast path for PinnedMeshDedupe: skip the hash for consecutive same-mesh entities.
            CMesh*                              LastPinnedMesh = nullptr;
            FFrameArenaAllocator                Arena;
            FSceneRenderStats                   Stats = {};

            FThreadLocalDrawData() = default;
            explicit FThreadLocalDrawData(FFrameArenaAllocator A)
                : Items(A), EntityRecords(A), LocalBatches(A), BonesData(A), PinnedMeshDedupe(A)
                , MaterialCache(A), Arena(A) {}
            
            void ResetForFrame(FFrameArenaAllocator A)
            {
                Items            = TFrameVector<FProcessedDrawItem>(A);
                EntityRecords    = TFrameVector<FEntityRecord>(A);
                LocalBatches     = TFrameVector<FLocalBatchEntry>(A);
                BonesData        = TFrameVector<FBoneTransform>(A);
                PinnedMeshDedupe = TFrameHashMap<CMesh*, uint8>(A);
                MaterialCache    = TFrameVector<FMaterialCacheEntry>(A);
                PinnedMeshBuffers.clear();
                LastPinnedMesh   = nullptr;
                Arena            = A;
                Stats            = {};
            }
        };

        // Shadow tile request captured during parallel light processing;
        // resolved + shrunk-to-fit by AllocateShadowTiles.
        struct FShadowRequest
        {
            uint32      LightIndex;
            ELightType  Type;
            uint32      DesiredPixels;
            float       DistanceToCamera;
            FVector3    Position;
            FVector3    Direction;      // Spot only
            FVector3    Up;             // Spot only
            float       Attenuation;
            float       OuterFOVDegrees;
        };

        // Per-frame extracted snapshot. FRAMES_IN_FLIGHT instances in FrameRing;
        // game thread writes one in Extract, render thread reads it in RenderView.
        struct FFrameData
        {
            struct FDecalBatch
            {
                // Ref-held shaders (not the live CMaterial*) so a deleted decal asset can't dangle
                // the render thread; the refcount keeps the shaders alive past the material's death.
                FRenderMaterialShaders Shaders;
                uint32      FirstInstance;
                uint32      Count;
            };

            // A run of glyph instances sharing one font atlas; one instanced draw per batch.
            struct FTextBatch
            {
                uint32      AtlasIndex   = 0;   // bindless ResourceID of the font atlas
                uint32      AtlasWidth   = 0;
                uint32      AtlasHeight  = 0;
                float       DistanceRange = 0.0f; // px range baked into the MSDF (drives shader AA)
                uint32      FirstInstance = 0;
                uint32      Count         = 0;
                bool        bDepthTest    = false; // occluded by + writes scene depth, vs. always-on-top
            };

            // Per-frame snapshot of one terrain; render passes read ONLY this. GPU resources live in
            // TerrainGPUStates keyed by Entity, so the render thread never dereferences the component.
            struct FTerrainExtract
            {
                entt::entity        Entity;
                FMatrix4            WorldMatrix;

                // Snapshot of the scalar params the render passes need.
                int32               Resolution      = 0;
                int32               ChunkResolution = 0;
                float               TileWorldSize   = 0.0f;
                float               MaxHeight       = 0.0f;
                int32               LayerCount       = 0;
                // Shaders resolved + ref-held on the game thread (never the live CMaterial*) so a
                // deleted terrain material can't dangle the render thread. Null VS => skip this terrain.
                FRenderMaterialShaders Shaders;
                uint32              MaterialIndex   = 0;
                bool                bCastShadow     = true;
                bool                bReceiveShadow  = true;

                // Dimensions changed this frame -> render thread (re)creates GPU textures
                // and the Full upload payloads below re-seed every slice.
                bool                bStructuralChange = false;

                // Height upload: 0 none, 1 full map, 2 packed dirty rect.
                uint8               HeightUpload    = 0;
                FIntVector2         HeightRectMin   = FIntVector2(0);
                FIntVector2         HeightRectMax   = FIntVector2(0);
                TVector<float>      HeightBytes;     // full map, or tightly-packed rect rows

                // Weight upload: 0 none, 1 all slices, 2 selected dirty slices.
                uint8               WeightUpload    = 0;
                uint32              WeightSliceMask = 0u;         // bit L set => slice L present
                TVector<uint8>      WeightBytes;     // dirty slices packed back-to-back

                // Chunk/meshlet metadata rebuilt this frame; copied so next frame's rebuild can't race the upload.
                bool                         bGeometryRebuilt = false;
                TVector<FTerrainChunkInfo>   Chunks;
                TVector<FTerrainMeshletInfo> Meshlets;
            };

            // Per-frame snapshot of one emitter; render passes read ONLY this. GPU + sim state lives in
            // ParticleGPUStates keyed by Entity, so the render thread never dereferences the component.
            struct FParticleExtract
            {
                entt::entity            Entity;
                FMatrix4                WorldMatrix;

                FVector3                EmitterOffset       = FVector3(0.0f);
                float                   TimeScale           = 1.0f;
                float                   SpawnRateMultiplier = 1.0f;
                bool                    bEmit               = true;
                bool                    bBurstOnSpawn       = true;

                // Game-thread Activate()/Deactivate() intents, applied once then cleared.
                bool                    bForceBurst         = false;
                bool                    bForceReset         = false;

                // Asset+override params resolved on the game thread.
                FResolvedParticleParams Resolved;

                bool                    bReady              = false;  // asset ready to simulate
                bool                    bUsesCustomShader   = false;
                FRHIComputeShaderRef    CustomComputeShader;          // set iff bUsesCustomShader
                uint32                  TextureIndex        = 0u;     // bindless slot, resolved game-side
            };

            // Per-frame snapshot of enabled capture views, in registration order. The shared cull fills
            // each view's slice; RenderView shades each into SceneViews[SceneViewIndex] after the primary.
            struct FCaptureViewData
            {
                FSceneGlobalData    SceneGlobalData = {};
                FViewVolume         ViewVolume      = {};
                uint32              CameraViewIndex = ~0u;   // its cull-view index (frustum-only)
                int32               SceneViewIndex  = -1;    // index into FForwardRenderScene::SceneViews
            };

            FViewVolume                      ViewVolume = {};
            FSceneGlobalData                 SceneGlobalData = {};
            SDefaultWorldSettings            CachedWorldSettings = {};
            float                            CachedWorldDeltaTime = 0.0f;
            bool                             bExtractedThisFrame = false;
            FSceneRenderStats                FrameStats = {};

            struct FGeometry
            {
                TVector<FGPUInstance>            Instances;
                TVector<FBoneTransform>          BonesData;   // 48B/bone (last row dropped)
                TVector<FSkinDescriptor>         SkinDescriptors;
                uint32                           TotalPreSkinnedVertices = 0;
                TVector<FMeshDrawCommand>        DrawCommands;
                TVector<uint32>                  OpaqueDrawList;
                TVector<uint32>                  OpaqueOccluderDrawList;
                TVector<uint32>                  TranslucentDrawList;
                TVector<FRHIBufferRef>           PinnedMeshBuffersThisFrame;
                FSceneCullContext                SceneCullContext;
                TVector<uint32>                  DrawMeshletStartOffsets;
                TVector<uint32>                  InstanceMeshletPrefix;
            } Geometry;

            struct FViews
            {
                TVector<FCullView>               CullViews;
                TVector<FDrawIndirectArguments>  IndirectArgs;
                uint32                           TotalMeshletBound   = 0;
                uint32                           NumDrawsPerView     = 0;
                uint32                           CameraLateViewIndex = ~0u;
                uint32                           CascadeViewBase     = ~0u;
                TVector<uint32>                  PointShadowCullViewBases;
                TVector<uint32>                  SpotShadowCullViewBases;
                TVector<FCaptureViewData>        CaptureViews;
            } Views;

            struct FLighting
            {
                FSceneLightData                  LightData = {};
                TArray<TVector<FLightShadow>, (uint32)ELightType::Num> PackedShadows = {};
                TAtomic<uint32>                  ShadowDataCount = 0;
                TVector<FShadowRequest>          ShadowRequests;
                FMutex                           ShadowRequestMutex;
                TVector<FShadowTile>             AtlasTiles;
            } Lighting;

            struct FPrimitives
            {
                TVector<FSimpleElementVertex>    SimpleVertices;
                TVector<FLineBatch>              LineBatches;
                TVector<FSimpleElementVertex>    SolidVertices;
                TVector<FSolidBatch>             SolidBatches;
                TVector<FBillboardInstance>      BillboardInstances;
                TVector<FGPUDecal>               DecalExtracts;
                TVector<FDecalBatch>             DecalBatches;
                TVector<FWidgetInstance>         WidgetInstances;
                TVector<FRHIImageRef>            PinnedWidgetRTs;
                TVector<FGPUGlyph>               GlyphInstances;
                TVector<FTextBatch>              TextBatches;
                TVector<FRHIImageRef>            PinnedFontAtlases;

                TVector<FGPUGlyph>               DebugTextGlyphs;
                FTextBatch                       DebugTextBatch;
            } Primitives;

            struct FVolumetrics
            {
                FEnvironmentParams               EnvironmentParams = {};
                FRHIImage*                       EnvironmentMapImage = nullptr;
                FExponentialHeightFogParams      FogParams           = {};
                bool                             bHasFog             = false;
                bool                             bVolumetricFog      = false;
                uint32                           VolumetricStepCount = 16;
                bool                             bIBLDirty            = false;
                bool                             bIBLConvolutionDirty = false;
                FIBLBakeResolution               IBLResolution        = {};
            } Volumetrics;

            // Post-process material resolved + ref-held on the game thread; the render thread reads
            // these instead of dereferencing a (possibly deleted) CMaterial.
            struct FPostProcessMaterial
            {
                FRenderMaterialShaders Shaders;
                uint32                 MaterialIndex = 0;
            };

            struct FPostProcess
            {
                SPostProcessSettings             ActivePostProcessStorage = {};
                bool                             bHasActivePostProcess = false;
                TVector<FPostProcessMaterial>    ActivePostProcessMaterials;
            } PostProcess;

            struct FExtracts
            {
                TVector<FTerrainExtract>         TerrainExtracts;
                TVector<entt::entity>            LiveTerrainEntities;
                TVector<FParticleExtract>        ParticleExtracts;
                TVector<entt::entity>            LiveParticleEntities;
                TVector<FTexturePaintOp>         PaintOps;
            } Extracts;

            struct FWater
            {
                TVector<FGPUWater>               Surfaces;
                bool                             bUnderwaterActive = false;
                FWaterUnderwaterParams           Underwater = {};
            } Water;
        };

        enum class ENamedImage : uint8
        {
            HDR,
            LDR,
            // Ping-pong scratch for the post-process material chain; same size/format
            // as LDR. Pass alternates LDR/this so each material reads the previous output.
            PostProcessScratch,
            SMAAEdges,
            SMAABlend,
            SMAAArea,
            SMAASearch,

            // Screen-space ambient occlusion factor (R8). SSAOPass writes it from reconstructed
            // depth+normals; SSAOBlurPass box-blurs it into SSAOBlur, which the base pass samples.
            SSAO,
            SSAOBlur,
            Cascade,
            DepthAttachment,
            DepthPyramid,
            Picker,
            Accum,
            Revealage,

            // Full-res RGBA16F copy of the lit HDR scene, blitted before the water + underwater passes so they
            // can sample the scene behind the surface (refraction / SSR / distortion) without reading back the
            // HDR target they also write.
            WaterRefraction,

            // DBuffer decal targets (RGBA8). A = transmittance (cleared to 1). A: BaseColor, B: WorldNormal
            // (OctEncode01 in rg), C: Roughness/Metallic/AO. Sampled+composited by the base pass before lighting.
            DBufferA,
            DBufferB,
            DBufferC,

            // Persistent 1x1 R32F eye-adapted luminance (AutoExposure writes, ColorGrading reads).
            // Not ring-buffered: adaptation feedback reads its own previous value.
            AdaptedLuminance,

            // Froxel volumetric fog 3D volumes (RGBA16F). Scatter = per-froxel (in-scatter, extinction);
            // Integrated = front-to-back accumulated (in-scatter, transmittance).
            FroxelScatter,
            FroxelIntegrated,

            // MSAA scratch RTs (allocated only when MSAASampleCount > 1); resolved into the 1x image at end-of-pass.
            HDR_MS,
            Depth_MS,
            Picker_MS,

            // Pre-integrated BRDF LUT for split-sum IBL (Karis 2013). Baked once; swapchain-independent.
            BRDFLut,

            // Sky cubemap (SkyCubeCapture.slang) feeding IBL convolution. Persistent; refreshes each frame sky is on.
            SkyCube,

            // Diffuse-BRDF-convolved sky; sampled by normal N for IBL diffuse.
            SkyIrradiance,

            // GGX-prefiltered sky, one mip per roughness step; sampled by reflection R.
            SkyPrefilter,

            #if USING(WITH_EDITOR)
            PointLightIcon,
            DirectionalLightIcon,
            SkyLightIcon,
            SpotLightIcon,
            CameraIcon,
            CharacterIcon,
            ParticleSystemIcon,
            #endif

            Num,
        };

        // Per-output-view rendering state. Geometry/lights gather once (shared members below), then
        // shading runs per FSceneView into its own image chain. Index 0 is primary; rest are captures.
        struct FSceneView
        {
            FRHIViewportRef                                 Viewport;
            FViewportState                                  ViewportState;
            FUIntVector2                                      Size = FUIntVector2(0);
            bool                                            bIsPrimary = false;

            // Capture views only: camera to render from (set each frame before Extract) and whether
            // it renders this frame. The primary view's camera comes from Extract's argument.
            FViewVolume                                     PendingViewVolume;
            bool                                            bEnabled = false;

            // Indexed by ENamedImage. Per-view slots own their image; view-independent slots (BRDF LUT,
            // sky cubes, SMAA LUTs, editor icons) alias the shared image so reads go through CurrentView.
            TArray<FRHIImageRef, (int)ENamedImage::Num>     Images = {};
            FRHIImageRef                                    BloomChainImage;

            // Per-view cluster grid (GPU-written, reached by device address via the scene root).
            FRHIBufferRef                                   ClusterBuffer;
            // All scene data (camera, lights, instances, ...) now reaches shaders by device address
            // through FSceneRoot, so a view owns no descriptor sets.

            // Cluster-grid cache: view-space AABBs depend only on this view's projection
            // + RT size, so the build is skipped while those are unchanged.
            FMatrix4                                       LastClusterInvProjection = FMatrix4(0.0f);
            FVector2                                       LastClusterNearFar       = FVector2(0.0f);
            FUIntVector2                                      LastClusterScreenSize    = FUIntVector2(0);
            bool                                            bClusterGridDirty        = true;
        };

        void Init() override;
        void Shutdown() override;
        
        void BeginFrame() override { }
        void EndFrame() override { }
        
        void Extract(const FViewVolume& ViewVolume, const SPostProcessSettings* PostProcess) override;
        void RenderView_RenderThread(ICommandList& CmdList, uint8 FrameIndex) override;
        void SignalFrameConsumed(uint8 FrameIndex) override;
        void SetActivePostProcessMaterials(const TVector<CMaterialInterface*>& Materials) override { PendingPostProcessMaterials = Materials; }
        void SwapchainResized(FVector2 NewSize);
        void Resize(const FUIntVector2& NewSize) override { SwapchainResized(FVector2(NewSize)); }

        int32 RegisterCaptureView(const FUIntVector2& Size) override;
        void  SetCaptureView(int32 Handle, const FViewVolume& View, bool bEnabled) override;
        FRHIImage* GetCaptureRenderTarget(int32 Handle) const override;
        
        void DrawBillboard(FRHIImage* Image, const FVector3& Location, float Scale) override;
        void DrawLine(const FVector3& Start, const FVector3& End, const FVector4& Color, float Thickness, bool bDepthTest, float Duration) override { }

        static FViewportState MakeViewportStateFromImage(const FRHIImage* Image);
        
        FRHIBuffer* GetPreSkinnedVerticesBuffer() const { return PreSkinnedVerticesBuffer; }
        // Per-view images route through CurrentView; shared slots are aliased into every view.
        // Falls back to the shared store during Init before any view exists.
        FRHIImage* GetNamedImage(ENamedImage Image) const { return CurrentView ? CurrentView->Images[(int)Image] : NamedImages[(int)Image]; }

        // Ringed accessors for the cull-pass scratch (see IndirectArgsRing).
        FRHIBuffer* GetIndirectArgs()     const { return IndirectArgsRing[CurrentFrameSlot]; }
        FRHIBuffer* GetMeshletDrawList()  const { return MeshletDrawListRing[CurrentFrameSlot]; }
        FRHIBuffer* GetMeshletDeferList() const { return MeshletDeferListRing[CurrentFrameSlot]; }
        FRHIBuffer* GetDeferCount()       const { return DeferCountRing[CurrentFrameSlot]; }
        FRHIBuffer* GetSpdCounter()       const { return SpdCounterRing[CurrentFrameSlot]; }

        // MSAA scratch RT when enabled, else the 1x image; use for the render-target
        // binding on geometry passes that participate in MSAA. 1x image is the resolve target.
        FRHIImage* GetSceneColorRT() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::HDR_MS) : GetNamedImage(ENamedImage::HDR); }
        FRHIImage* GetSceneDepthRT() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::Depth_MS) : GetNamedImage(ENamedImage::DepthAttachment); }
        FRHIImage* GetPickerRT()     const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::Picker_MS) : GetNamedImage(ENamedImage::Picker); }

        /** Resolve target, null when MSAA off (no resolve needed). Caller adds via FAttachment::SetResolveImage. */
        FRHIImage* GetSceneColorResolve() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::HDR) : nullptr; }
        FRHIImage* GetSceneDepthResolve() const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::DepthAttachment) : nullptr; }
        FRHIImage* GetPickerResolve()     const { return MSAASampleCount > 1 ? GetNamedImage(ENamedImage::Picker) : nullptr; }

        uint8 GetMSAASampleCount() const { return MSAASampleCount; }
        
        FRHIImage* GetRenderTarget() const override;
        const FSceneRenderStats& GetRenderStats() const override;
        FSceneRenderSettings& GetSceneRenderSettings() override;
        entt::entity GetEntityAtPixel(uint32 X, uint32 Y) const override;
    #if USING(WITH_EDITOR)
        void SetPickerCursor(uint32 X, uint32 Y, bool bOverViewport) override;
    #endif
        const FShadowAtlas* GetShadowAtlas() const override { return &ShadowAtlas; }
        
        
    private:

        // Shared (view-independent) per-frame buffers: instances, bones, lights, cull
        // views, the cull-scratch rings, etc. Per-view Scene/Cluster buffers live on FSceneView.
        void InitBuffers();
        // Per-view image chain (HDR/LDR/Depth/Pyramid/Picker/post scratch/SMAA/bloom/...)
        // sized to View.Viewport's render target. Shared images are aliased into View.Images.
        void InitViewImages(FSceneView& View);
        // (Re)builds the primary view's images/buffers/binding sets + viewport state. Called
        // on init and swapchain resize.
        void InitFrameResources();
        // Per-view binding sets (scene rw/ro per slot, compose, SMAA, OIT) against the
        // shared layouts, referencing this view's per-view buffers/images.
        void CreateViewBindingSets(FSceneView& View);

        // Allocates + wires a new FSceneView (per-view images, Scene/Cluster buffers, binding
        // sets) around an existing viewport/RT, appends it to SceneViews, and returns it.
        FSceneView& AddSceneView(const FRHIViewportRef& Viewport, bool bPrimary);

        // Render thread: shade the current capture view (CurrentView) into its RT. Reduced single-pass
        // (no two-pass occlusion); geometry/shadows/sky were already produced by the shared passes.
        void RenderCaptureView(ICommandList& CmdList);

        // The view being shaded's final output RT (the live SceneViewport's RT). Primary and
        // capture passes write here; GetRenderTarget() (public) always returns the primary's.
        FRHIImage* GetViewOutputTarget() const { return SceneViewport ? SceneViewport->GetRenderTarget() : nullptr; }

        // Re-point CurrentView + every live per-view member (binding sets, viewport, bloom) at View
        // so the shading passes operate on it. Called once per view in RenderView_RenderThread.
        void PointAtView(FSceneView& View)
        {
            CurrentView                = &View;
            BloomChain                 = View.BloomChainImage;
            SceneViewportState         = View.ViewportState;
            SceneViewport              = View.Viewport.GetReference();
        }

        // Aliases the process-wide immutable resources (BRDF LUT, SMAA LUTs, editor
        // icons) into this scene's NamedImages, building them once on the first scene.
        void InitSharedResources();

        // Bakes the BRDF integration LUT and returns it (no NamedImages assignment).
        FRHIImageRef BakeBRDFLUT();
        
        void InitSkyCube(uint32 FaceSize);

        void InitIBLConvolutionTargets(const FIBLBakeResolution& Resolution);

        // Render thread: recreate the sky/irradiance/prefilter cubes when the active environment's
        // IBL quality changes. WaitIdle-guarded (rare, editor-driven); patches every view's shared-image
        // snapshot and forces a re-bake. No-op when Resolution already matches AppliedIBLResolution.
        void SyncIBLResolution(const FIBLBakeResolution& Resolution);

        //~ Begin Render Passes
        // Game thread: clear per-frame CPU scene state before the parallel gather repopulates.
        void ResetPass_GameThread();

        // Render thread: depth + shadow atlas clears recorded onto the cmd list.
        void ResetPass_RenderThread(ICommandList& CmdList);
        
        void CullPassEarly(ICommandList& CmdList);
        void CullPassLate(ICommandList& CmdList);

        // Skins every visible skeletal entity once into the pre-skinned vertex buffer,
        // before any draw pass that reads skinned geometry.
        void SkinningPass(ICommandList& CmdList);

        // Replays Frame.PaintOps as compute brush dispatches into each target's bindless UAV, then
        // restores them to ShaderResource so the geometry passes can sample them.
        void TexturePaintPass(ICommandList& CmdList);

        
        void DepthPrePassEarly(ICommandList& CmdList);
        void DepthPrePassLate(ICommandList& CmdList);
        void DepthPyramidPass(ICommandList& CmdList);
        void ClusterBuildPass(ICommandList& CmdList);
        void LightCullPass(ICommandList& CmdList);
        void PointShadowPass(ICommandList& CmdList);
        void SpotShadowPass(ICommandList& CmdList);
        void CascadedShowPass(ICommandList& CmdList);
        void DecalPass(ICommandList& CmdList);
        void BasePass(ICommandList& CmdList);
        void BillboardPass(ICommandList& CmdList);
        void WidgetPass(ICommandList& CmdList);
        void TextPass(ICommandList& CmdList);
        void DebugTextPass(ICommandList& CmdList);
        void WidgetPickerPass(ICommandList& CmdList);
        void ParticleSimulatePass(ICommandList& CmdList);
        void ParticleRenderPass(ICommandList& CmdList);
        void TerrainUpdatePass(ICommandList& CmdList);
        void TerrainCullPass(ICommandList& CmdList);
        void TerrainDepthPrePass(ICommandList& CmdList);
        void TerrainRenderPass(ICommandList& CmdList);
        void SSAOPass(ICommandList& CmdList);
        void SSAOBlurPass(ICommandList& CmdList);
        void TransparentPass(ICommandList& CmdList);
        void OITResolvePass(ICommandList& CmdList);
        void FroxelInjectPass(ICommandList& CmdList);
        void FroxelIntegratePass(ICommandList& CmdList);
        void FroxelApplyPass(ICommandList& CmdList);
        void WaterPass(ICommandList& CmdList);
        void UnderwaterPass(ICommandList& CmdList);
        void EnvironmentPass(ICommandList& CmdList);
        void SkyCubeCapturePass(ICommandList& CmdList);
        void IrradianceConvolutionPass(ICommandList& CmdList);
        void PrefilterEnvMapPass(ICommandList& CmdList);
        void BatchedLineDraw(ICommandList& CmdList);
        void BatchedTriangleDraw(ICommandList& CmdList);
        void BloomPass(ICommandList& CmdList);
        void AutoExposurePass(ICommandList& CmdList);
        void ToneMappingPass(ICommandList& CmdList);
        void PostProcessMaterialPass(ICommandList& CmdList);
        void SMAAEdgeDetectionPass(ICommandList& CmdList);
        void SMAABlendWeightPass(ICommandList& CmdList);
        void SMAANeighborhoodBlendPass(ICommandList& CmdList);
        //~ End Render Passes

        // Game-thread half: ECS reads + parallel Process* tasks + cull/shadow setup.
        void CompileDrawCommands_GameThread();

        // Render-thread half: buffer resize + WriteBuffer commands; reads game-thread state.
        void CompileDrawCommands_RenderThread(ICommandList& CmdList);

        void ProcessStaticMeshEntityInternal(entt::entity Entity, const SStaticMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);
        void ProcessSkeletalMeshEntityInternal(entt::entity Entity, SSkeletalMeshComponent& MeshComponent, const STransformComponent& TransformComponent, FThreadLocalDrawData& Local);

        // Populates SceneCullContext with camera/shadow frustums and per-light spheres.
        // Runs serially before the parallel gather so the context is immutable during it.
        void BuildSceneCullContext();
        void MergeMeshDrawData(TVector<FThreadLocalDrawData>& ThreadLocal);
        
        void ProcessPointLight(const SPointLightComponent& PointLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessSpotLight(const SSpotLightComponent& SpotLight, const STransformComponent& TransformComponent, TAtomic<uint32>& LightCount);
        void ProcessDirectionalLight(const SDirectionalLightComponent& DirectionalLight, TAtomic<uint32>& LightCount);

        // Serial post-pass: fits shadow requests into the atlas budget (shrinks largest
        // tiles until sum(area) <= capacity), then allocates tiles + PackedShadows.
        void AllocateShadowTiles();

        // Builds the per-frame FCullView array and seeds IndirectArgs per view. Runs
        // after AllocateShadowTiles (ViewProjection filled) and before the buffer upload.
        void BuildCullViews(const FViewVolume& ViewVolume);
        
        // Line batching split into graph-schedulable phases so the heavy parallel cull is dispatched as a
        // first-class root node (its chunks enter the queue at Dispatch, alongside the mesh fan-out) instead
        // of being fired from inside a single node at runtime (which bubbled the pool). Prepare runs inline
        // before Dispatch (cheap: build chunk views + clear scratch, returns the chunk count); BatchLineChunks
        // is the parallel-for body; FinalizeBatchedLines merges/scatters/rebuilds after the batch node.
        uint32 PrepareBatchedLines(FLineBatcherComponent& Batcher);
        void   BatchLineChunks(const Task::FParallelRange& Range);
        void   FinalizeBatchedLines(FLineBatcherComponent& Batcher);
        void   ProcessBatchedTriangles(FTriangleBatcherComponent& Batcher);
        
        void NotifyMaxLightsHit();

        // CPU early-out for shadow requests: false when the light's attenuation sphere
        // is fully outside the camera frustum, skipping its shadow view/tile/slice.
        bool ShouldRequestShadow(const FVector3& LightPosition, float LightRadius) const;


    private:
        
        // The only persistent per-frame buffers left. Everything CPU-dynamic (instances/lights/bones/
        // billboards/widgets/cull views/skin descriptors/env+fog params/meshlet prefix) is uploaded to
        // the command-list transient ring each frame; the GPU-written cull/draw rings have own members.
        // Pre-skinned vertices are GPU-written device-local scratch (skinning compute writes, draw VS reads
        // via BDA). Debug line/triangle geometry is ring-allocated at its draw site, no persistent buffer.
        FRHIBufferRef                                   PreSkinnedVerticesBuffer;
        // Per-buffer hysteresis counters for ResizeBufferIfNeeded's shrink path (consecutive
        // frames of sustained low usage). Persisted so memory is reclaimed after the scene shrinks.
        uint32                                          PreSkinnedVerticesLowUsage = 0;
        TArray<uint32, FRAMES_IN_FLIGHT>                IndirectArgsRingLowUsage = {};
        TArray<uint32, FRAMES_IN_FLIGHT>                MeshletDrawListRingLowUsage = {};
        TArray<uint32, FRAMES_IN_FLIGHT>                MeshletDeferListRingLowUsage = {};
        TArray<FRHIImageRef, (int)ENamedImage::Num>     NamedImages = {};

        /** MSAA sample count cached from world settings. 1 == disabled (no overhead). */
        uint8                                           MSAASampleCount = 1;

        /** Allocate a view's MS-only scratch images (HDR_MS, Depth_MS, Picker_MS). No-op when MSAA is off. */
        void AllocateMSAAImages(FSceneView& View, const FUIntVector2& Extent);

        /** Reconcile cached sample count with the world setting; reallocates every view's MS images when it changes. */
        void SyncMSAAState();

        // Bloom mip chain (one image, BLOOM_MIP_COUNT mips). SPD downsample writes
        // mips 0..N-1 from HDR; upsample walks back additively. Tone-mapping samples mip 0.
        static constexpr uint32                         BLOOM_MIP_COUNT = 5;

        // SceneViews[0] is primary, rest are captures; reserved to MaxSceneViews and never grown past it,
        // since CurrentView is a raw pointer the render thread holds across a frame (a realloc would dangle it).
        static constexpr uint32                 MaxSceneViews = 16;
        TVector<FSceneView>                     SceneViews;
        FSceneView*                             CurrentView = nullptr;

        // Cull-view indices of the view being shaded; camera draw passes index the right IndirectArgs
        // slice. Primary: 0 / Frame.CameraLateViewIndex. Capture: frustum-only view / ~0u.
        uint32                                  CurrentCameraEarlyView = 0;
        uint32                                  CurrentCameraLateView  = ~0u;

        // Live pointer into CurrentView->BloomChainImage.
        FRHIImage*                              BloomChain = nullptr;

        FViewportState                          SceneViewportState;
        // Live pointer into CurrentView->Viewport.
        FRHIViewport*                           SceneViewport = nullptr;
        FDelegateHandle                         SwapchainResizedHandle;
        CWorld*                                 World = nullptr;

        // Mirror of last-rendered frame's FrameStats; rendered immediately
        // visible to GetRenderStats() callers (editor UI panels).
        FSceneRenderStats                       RenderStats;
        FSceneRenderSettings                    RenderSettings;

        // IBL change-tracking: persistent SkyCube/Irradiance/Prefilter rebuilt only when
        // these last-baked inputs change. bIBLConvolutionDirty gates the costly convolution.
        FEnvironmentParams                      LastIBLEnvironmentParams = {};
        FRHIImage*                              LastIBLEnvironmentMap    = nullptr;
        FVector3                               LastIBLSunDirection      = FVector3(0.0f);
        bool                                    bLastIBLHasSun           = false;
        bool                                    bIBLValid                = false;

        FEnvironmentParams                      LastConvolvedEnvironmentParams = {};
        FRHIImage*                              LastConvolvedEnvironmentMap    = nullptr;
        FVector3                               LastConvolvedSunDirection      = FVector3(0.0f);
        bool                                    bLastConvolvedHasSun           = false;
        bool                                    bIBLConvolutionValid           = false;

        // Currently-allocated IBL cube resolution (render thread). SyncIBLResolution rebuilds the cubes
        // when the extracted FVolumetrics::IBLResolution differs. Init() allocates at this default (High).
        FIBLBakeResolution                      AppliedIBLResolution           = {};
        // Game-thread last-extracted resolution; a change forces bIBLDirty so the new cubes get baked.
        FIBLBakeResolution                      LastExtractedIBLResolution     = {};

        // Mirrors last-seen EnvironmentParams; a memcmp gate skips the costly IBL convolution
        // for the common static-environment case.
        FEnvironmentParams                      LastUploadedEnvironmentParams = {};
        bool                                    bEnvironmentParamsUploaded    = false;

        FBindingCache                           BindingCache;

        // SSAO setup, built once in Init(): the 4x4 tangent-rotation noise texture (sampled tiled with
        // a wrap point sampler) and the cached hemisphere kernel copied into each frame's SceneGlobalData.
        FRHIImageRef                            SSAONoiseTexture;
        FSSAOSettings                           CachedSSAOSettings = {};

        // Latest post-process material list from the world; captured into
        // FFrameData::ActivePostProcessMaterials at the start of Extract.
        TVector<CMaterialInterface*>            PendingPostProcessMaterials;

        // Bindless scene model: shared (per-frame) buffer addresses filled in CompileDrawCommands; the
        // per-view root (adds this view's camera + clusters + IBL indices) is uploaded to a transient
        // and its address kept in CurrentSceneRootAddr, which every pass pushes as FRootConstants.RootAddr.
        FSceneRoot                                                      SceneRootShared = {};
        uint64                                                          CurrentSceneRootAddr = 0;
        // Builds the per-view FSceneRoot transient (shared addrs + view camera/clusters/IBL) -> address.
        uint64 BuildViewSceneRoot(ICommandList& CmdList, FSceneView& View, uint64 SceneDataAddr);

        // Push the engine-wide root constants for a pass: the current view's scene root + this pass's
        // own constants (uploaded to a transient). Replaces every per-pass SetPushConstants.
        template<typename T> void PushRootConstants(ICommandList& CmdList, const T& PassData);
        void PushRootConstants(ICommandList& CmdList);   // pass with no own constants

        // GPU-atomic-written by CullMeshlets, consumed by DrawIndirect, so it can't be
        // BUF_Dynamic (no dynamic offset to vkCmdDrawIndirect). Manual N-buffer ring.
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         IndirectArgsRing = {};

        // Cull-pass scratch, ringed like IndirectArgsRing so frame N+1's cull overlaps
        // frame N's draws instead of stalling on a WAR barrier on one shared buffer.
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         MeshletDrawListRing = {};
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         MeshletDeferListRing = {};
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         DeferCountRing = {};
        TArray<FRHIBufferRef, FRAMES_IN_FLIGHT>                         SpdCounterRing = {};

        // Frame slot index for ringed scene resources. Set at the top of
        // RenderView_RenderThread from GRenderManager->GetCurrentFrameIndex().
        uint8                                                           CurrentFrameSlot = 0;

        FShadowAtlas                            ShadowAtlas;

        // Render-thread-owned terrain GPU resources keyed by entity, decoupled from STerrainComponent
        // lifetime; reclaimed in TerrainUpdatePass via FFrameData::LiveTerrainEntities.
        THashMap<entt::entity, FTerrainGPUState> TerrainGPUStates;

        // Render-thread-owned particle GPU + sim state, keyed by entity. Same decoupling as
        // TerrainGPUStates; reclaimed in ParticleSimulatePass via FFrameData::LiveParticleEntities.
        THashMap<entt::entity, FParticleGPUState> ParticleGPUStates;

        // One bump arena per worker thread, reset each frame; backs all per-frame
        // TFrameVector / TFrameHashMap members on FThreadLocalDrawData.
        TVector<TUniquePtr<FBlockLinearAllocator>> FrameArenas;

        // MergeMeshDrawData scratch, persisted across frames (grown, never shrunk);
        // the active prefix is overwritten in place each frame.
        TVector<FDrawBatchKey>                  MergeGlobalBatchKeys;
        TVector<TVector<FLocalBatchEntry*>>     MergeBatchToLocals;
        TVector<TVector<FDrawKey>>              MergeGlobalDrawsPerBatch;
        TVector<uint32>                         MergeBatchDrawArgBase;
        TVector<uint32>                         MergeDrawInstanceCounts;
        TVector<uint32>                         MergeMeshletCountsPerDraw;
        TVector<uint32>                         MergeDrawInstanceOffsets;
        TVector<uint32>                         MergeDrawCursor;
        TVector<uint32>                         MergeThreadBoneBase;

        struct FDecalSortEntry { CMaterial* ShaderOwner; int32 SortOrder; FGPUDecal Gpu; };
        TVector<FDecalSortEntry>                DecalSortScratch;
        THashMap<CMaterial*, int32>             DecalGroupMinSort;

        // Per-worker draw-gather scratch, persisted so outer storage keeps capacity;
        // arena-backed members are reset each frame (ResetForFrame) to avoid aliasing.
        TVector<FThreadLocalDrawData>           ThreadLocalStorage;

        // Per-worker scratch for parallel debug-line batching; persisted across frames
        // (vectors keep capacity, cleared each frame). Each worker buckets its culled
        // lines by (thickness, depth-test) locally; a serial merge lays the per-bucket
        // ranges out contiguously, then a parallel pass copies them into SimpleVertices.
        struct alignas(64) FLineBatchScratch
        {
            static constexpr uint32 kMaxBuckets = 16;
            float    BucketThickness[kMaxBuckets];
            uint8    BucketDepthTest[kMaxBuckets];
            uint32   GlobalBucket[kMaxBuckets];     // local -> global index, filled at merge
            uint32   WriteCursor[kMaxBuckets];      // vertex write offset, filled at merge
            uint32   NumBuckets = 0;
            TVector<FSimpleElementVertex>                  BucketVerts[kMaxBuckets];
            TVector<FLineBatcherComponent::FLineInstance>  Survivors;
        };
        TVector<FLineBatchScratch>              LineBatchScratch;
        // Reused as the compacted persistent-line buffer so survivor rebuilds don't churn the heap.
        TVector<FLineBatcherComponent::FLineInstance> LineCompactScratch;

        // Fixed-size views over the batcher's per-worker buffers + persistent list, built each frame as the
        // balanced work units for the parallel line batch (no drain). Reused so it doesn't churn the heap.
        struct FLineChunk { const FLineBatcherComponent::FLineInstance* Data; uint32 Count; };
        TVector<FLineChunk>                     LineChunkScratch;
        
        FTaskGraph                              DrawTaskGraph;
        FTaskGraph                              DedupTaskGraph;

        TArray<FFrameData,      FRAMES_IN_FLIGHT>       FrameRing;
        TArray<TAtomic<uint64>, FRAMES_IN_FLIGHT>       SlotConsumedCount;
        TArray<uint64,          FRAMES_IN_FLIGHT>       SlotProducedCount = {};
        TArray<TAtomic<bool>,   FRAMES_IN_FLIGHT>       SlotHasPendingConsume = {};
        FMutex                                          SlotMutex;
        std::condition_variable                         SlotCV;

        FFrameData*                             ExtractFrame = nullptr;  // game thread
        FFrameData*                             RenderFrame  = nullptr;  // render thread

        void WaitForSlotConsumed(uint8 Slot, uint64 Target);
        void SignalSlotConsumed(uint8 Slot);

#if USING(WITH_EDITOR)
        static constexpr uint32                 PickerReadbackRingSize = FRAMES_IN_FLIGHT + 1;
        // Side of the square copied around the cursor; large enough to still contain
        // the click pixel after the few-frame cursor drift before readback consumes it.
        static constexpr uint32                 PickerRegionExtent = 64;
        struct FPickerReadbackSlot
        {
            FRHIStagingImageRef Staging;
            uint32              OriginX = 0;        // top-left of the copied region, in picker texels
            uint32              OriginY = 0;
            uint32              Width = 0;          // region dimensions == staging image extent
            uint32              Height = 0;
            uint64              SubmittedFrame = 0;
            bool                bPending = false;
        };
        mutable TArray<FPickerReadbackSlot,     PickerReadbackRingSize> PickerReadbackRing;
        uint64                                  PickerReadbackFrame = 0;
        uint32                                  PickerReadbackWriteIndex = 0;

        // Pick-cursor published by editor, read by readback. Packed into one atomic to
        // avoid torn reads: bit 0 = over-viewport, bits 1..21 = X, bits 22..42 = Y.
        TAtomic<uint64>                         PickerCursorPacked = 0;

        // Schedules the per-frame picker -> staging copy after the last picker RT write.
        void IssuePickerReadback(ICommandList& CmdList);
#endif

    };
}
