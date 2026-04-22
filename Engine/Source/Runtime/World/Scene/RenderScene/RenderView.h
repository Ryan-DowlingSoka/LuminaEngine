#pragma once

#include "Containers/Array.h"
#include "Core/Assertions/Assert.h"
#include "Core/Math/Frustum.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RHIFwd.h"

namespace Lumina
{
    /**
     * A RenderView fully describes a single viewpoint from which the world
     * is rasterized. The scene populates a FViewRegistry each frame with
     * one view per visible vantage: the primary camera, each directional
     * cascade, each shadowed point light, each shadowed spot light. Passes
     * that submit geometry iterate the registry by kind and emit the same
     * GPU work for every view, parameterized only by the view's own state
     * (matrices, output target, push constants, indirect-args slice).
     *
     * The goal is that adding a new vantage (e.g. a reflection capture, a
     * portal, a secondary camera) is a matter of pushing a new FRenderView
     * into the registry rather than introducing a new bespoke render pass.
     */

    enum class ERenderViewKind : uint8
    {
        Main,                   // Primary camera view (base / depth / transparency).
        ShadowDirectional,      // One cascade of a directional CSM split.
        ShadowPoint,            // A shadowed point light (rasterized layered, 6 faces).
        ShadowSpot,             // A shadowed spot light (single-layer atlas tile).

        Num,
    };

    /**
     * Describes where a view's rasterized geometry lands: the output image,
     * an array slice (or span of slices for layered / multiview rendering),
     * and the viewport + scissor within that slice. Kept separate from
     * FRenderView so several views can target different layers of the same
     * atlas image without duplicating anything else.
     */
    struct FViewTarget
    {
        FRHIImage*  DepthAttachment     = nullptr;
        FRHIImage*  ColorAttachment     = nullptr;  // Optional; currently main view only.
        FRHIImage*  PickerAttachment    = nullptr;  // Optional; currently main view only.

        // Layered rendering. ArraySlice is the first targeted layer; NumSlices
        // spans consecutive layers (NumSlices > 1 requires ViewMask != 0 to
        // route primitives via multiview on Vulkan).
        uint16      ArraySlice          = 0;
        uint16      NumSlices           = 1;
        uint32      ViewMask            = 0;

        glm::uvec2  RenderArea          = glm::uvec2(0, 0);
        FViewport   Viewport;
        FRect       Scissor;

        float       DepthClearValue     = 1.0f;
    };

    /**
     * One vantage into the scene. Matrix + frustum drive culling and
     * per-view uniform data; Target drives attachment binding; PushConstants
     * and IndirectDrawBaseOffset parameterize the submission helper.
     */
    struct FRenderView
    {
        ERenderViewKind Kind = ERenderViewKind::Main;

        glm::vec3   ViewPosition        = glm::vec3(0.0f);
        glm::mat4   ViewMatrix          = glm::mat4(1.0f);
        glm::mat4   ProjectionMatrix    = glm::mat4(1.0f);
        glm::mat4   ViewProjection      = glm::mat4(1.0f);
        FFrustum    Frustum;

        float       Near                = 0.0f;
        float       Far                 = 1.0f;

        FViewTarget Target;

        // Per-view push constants. 16 bytes covers every current call site
        // (CSM pushes 8 B; point/spot shadows push 8 B; main pushes nothing).
        uint8       PushConstants[16]   = {};
        uint8       PushConstantSize    = 0;

        // Added on top of FMeshDrawCommand::IndirectDrawOffset when this
        // view dispatches an indirect draw on the legacy (VS) path. Non-zero
        // only for CSM cascades, whose legacy indirect buffer is laid out
        // cascade-major: [c * NumDraws + d].
        uint32      IndirectDrawBaseOffset = 0;

        // Payload for a per-view ID the shader can read (e.g. cascade index,
        // shadow-data index, light index). Matches the single u32/u64 pushes
        // used by the shadow shaders.
        template <typename T>
        void SetPushConstants(const T& Payload)
        {
            static_assert(sizeof(T) <= sizeof(PushConstants), "Push-constant payload too large for FRenderView");
            memcpy(PushConstants, &Payload, sizeof(T));
            PushConstantSize = (uint8)sizeof(T);
        }

        bool HasPushConstants() const { return PushConstantSize > 0; }
    };

    /**
     * Per-frame registry of views, indexed by kind. Populated by the scene
     * during light / camera setup; consumed by geometry-submission passes.
     */
    class FViewRegistry
    {
    public:

        void BeginFrame()
        {
            for (TVector<FRenderView>& Bucket : Buckets)
            {
                Bucket.clear();
            }
        }

        FRenderView& Add(ERenderViewKind Kind)
        {
            TVector<FRenderView>& Bucket = Buckets[(uint32)Kind];
            FRenderView& View = Bucket.emplace_back();
            View.Kind = Kind;
            return View;
        }

        TSpan<const FRenderView> GetViews(ERenderViewKind Kind) const
        {
            const TVector<FRenderView>& Bucket = Buckets[(uint32)Kind];
            return TSpan<const FRenderView>(Bucket.data(), Bucket.size());
        }

        const FRenderView* GetMainView() const
        {
            const TVector<FRenderView>& Main = Buckets[(uint32)ERenderViewKind::Main];
            return Main.empty() ? nullptr : &Main.front();
        }

        bool Empty(ERenderViewKind Kind) const
        {
            return Buckets[(uint32)Kind].empty();
        }

    private:

        TArray<TVector<FRenderView>, (uint32)ERenderViewKind::Num> Buckets;
    };
}
