#pragma once

#include "imgui.h"
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
        void EndFrame(const FUpdateContext& UpdateContext, ICommandList& CmdList);

        virtual void OnStartFrame(const FUpdateContext& UpdateContext) = 0;
        virtual void OnEndFrame(const FUpdateContext& UpdateContext, ICommandList& CmdList) = 0;

        virtual ImTextureID GetOrCreateImTexture(FStringView Path) = 0;
        virtual ImTextureID GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources = AllSubresources) = 0;
        virtual void DestroyImTexture(uint64 Hash) = 0;

        virtual void DrawRenderDebugInformationWindow(bool* bOpen, const FUpdateContext& Context) = 0;
        
        RUNTIME_API ImGuiContext* GetImGuiContext() const { return Context; }
        RUNTIME_API ImPlotContext* GetImPlotContext() const { return ImPlotContext; }
        
    protected:

        ImGuiContext* Context = nullptr;
        ImPlotContext* ImPlotContext = nullptr; 
        
    };
}
