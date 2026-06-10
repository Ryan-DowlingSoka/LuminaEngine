#pragma once

#include "Memory/SmartPtr.h"
#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "Renderer/RHI.h"
#include "Renderer/RHITexture.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"

struct ImDrawData;
struct ImTextureData;

namespace Lumina
{
    class FUpdateContext;

    // New-RHI ImGui backend: records draws via RHI:: into the swapchain image, samples the
    // global texture heap by ResourceID. ImGui_ImplGlfw kept for input; one DrawIndexed per
    // ImDrawCmd, vertex-pull by device address. Fonts live in RHI::Textures (new heap).
    class FVulkanImGuiRender : public IImGuiRenderer
    {
    public:

        void Initialize() override;
        void Deinitialize() override;

        void OnStartFrame(const FUpdateContext& UpdateContext) override;
        void OnEndFrame_NewRHI(RHI::FCmdListH CL, RHI::FTextureH Target, const FUIntVector2& Extent, FImDrawDataSnapshot& Snapshot) override;
        void ProcessTextureUpdates_GameThread() override;

        RUNTIME_API ImTextureID GetOrCreateImTexture(FStringView Path) override;

    private:

        // Record one ImDrawData into the swapchain image (Target) via RHI::. Clears, then one
        // DrawIndexed per ImDrawCmd with per-cmd scissor + args (vertex-pull, bindless new heap).
        void RecordDrawData_NewRHI(RHI::FCmdListH CL, ImDrawData* DrawData, RHI::FTextureH Target, const FUIntVector2& Extent);

        // Pipeline (BGRA8 swapchain), depth-disabled state, and the font atlases living in the
        // new texture heap (keyed by ImTextureData::UniqueID).
        RHI::FPipelineUH                       NewPipeline;
        RHI::FDepthStencilUH                   NewDepthState;
        THashMap<int32, RHI::FManagedTexture>  NewFontTextures;

        // Path-loaded UI images (icons), decoded straight into the new texture heap so their
        // ImTextureID is a new-heap ResourceID. Cached + reused across frames.
        THashMap<FName, RHI::FManagedTexture>  PathTextures;

        mutable FRecursiveMutex                Mutex;
    };
}
