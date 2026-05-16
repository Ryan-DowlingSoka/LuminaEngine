#pragma once

#include "imgui.h"
#include "ImDrawDataSnapshot.h"
#include "ImGuiX.h"
#include "Renderer/RenderResource.h"
#include "Subsystems/Subsystem.h"

struct ImPlotContext;

namespace Lumina
{
    class ICommandList;
    class FRenderManager;
}

namespace Lumina
{
    class IImGuiRenderer
    {
    public:

        virtual ~IImGuiRenderer() = default;

        virtual void Initialize();
        virtual void Deinitialize();

        void StartFrame(const FUpdateContext& UpdateContext);

        // Game thread: ImGui::Render() then deep-copy DrawData into a heap
        // snapshot. The caller (render command lambda) owns and destroys it.
        TUniquePtr<FImDrawDataSnapshot> BuildFrame_GameThread();

        // Render thread: record the snapshot's draw lists onto CmdList.
        void RecordFrame_RenderThread(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot);

        virtual void OnStartFrame(const FUpdateContext& UpdateContext) = 0;
        virtual void OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot) = 0;

        // Copy this frame's referenced texture refs into the snapshot so the
        // render thread keeps them alive while recording.
        virtual void FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out) = 0;

        virtual ImTextureID GetOrCreateImTexture(FStringView Path) = 0;
        virtual ImTextureID GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources = AllSubresources) = 0;
        virtual void DestroyImTexture(uint64 Hash) = 0;

        // Draws the renderer-debug body (tabs + content) into the current ImGui window.
        // Caller owns the surrounding window — this just emits content.
        virtual void DrawRenderDebugContents(const FUpdateContext& Context) = 0;
        
        RUNTIME_API ImGuiContext* GetImGuiContext() const { return Context; }
        RUNTIME_API ImPlotContext* GetImPlotContext() const { return ImPlotContext; }
        
    protected:

        ImGuiContext* Context = nullptr;
        ImPlotContext* ImPlotContext = nullptr; 
        
    };
}
