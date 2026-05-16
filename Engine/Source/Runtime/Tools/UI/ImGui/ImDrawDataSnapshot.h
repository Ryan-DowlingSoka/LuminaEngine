#pragma once
#include "imgui.h"
#include "Containers/Array.h"
#include "Memory/SmartPtr.h"
#include "ModuleAPI.h"
#include "Renderer/RenderResource.h"


namespace Lumina
{
    // Deep copy of an ImDrawData frame, owned by the render-thread command that
    // consumes it. Lets the game thread call ImGui::NewFrame for the next frame
    // without invalidating draw data the render thread is still reading.
    class FImDrawDataSnapshot
    {
    public:

        FImDrawDataSnapshot() = default;
        ~FImDrawDataSnapshot() { Reset(); }

        FImDrawDataSnapshot(const FImDrawDataSnapshot&)            = delete;
        FImDrawDataSnapshot& operator=(const FImDrawDataSnapshot&) = delete;

        // Texture refs the render thread must keep alive for the duration of
        // its recording; populated by the backend via FillReferencedImagesSnapshot.
        TVector<FRHIImageRef> ReferencedImages;

        // Source may be null (e.g. minimized).
        void CopyFrom(const ImDrawData* Source)
        {
            Reset();
            if (Source == nullptr)
            {
                return;
            }

            // Do NOT shallow-assign `DrawData = *Source` then clear() the
            // CmdLists -- ImVector::operator= aliases the source Data pointer,
            // and clear() then IM_FREEs it, corrupting ImGui's live state.
            DrawData.Valid              = Source->Valid;
            DrawData.CmdListsCount      = Source->CmdListsCount;
            DrawData.TotalIdxCount      = Source->TotalIdxCount;
            DrawData.TotalVtxCount      = Source->TotalVtxCount;
            DrawData.DisplayPos         = Source->DisplayPos;
            DrawData.DisplaySize        = Source->DisplaySize;
            DrawData.FramebufferScale   = Source->FramebufferScale;
            DrawData.OwnerViewport      = Source->OwnerViewport;
            DrawData.Textures           = Source->Textures;

            ClonedCmdLists.reserve(Source->CmdLists.Size);
            for (ImDrawList* SrcList : Source->CmdLists)
            {
                if (SrcList == nullptr)
                {
                    continue;
                }
                ImDrawList* Clone = SrcList->CloneOutput();
                ClonedCmdLists.push_back(Clone);
                DrawData.CmdLists.push_back(Clone);
            }

            bValid = true;
        }

        void Reset()
        {
            for (ImDrawList* L : ClonedCmdLists)
            {
                IM_DELETE(L);
            }
            ClonedCmdLists.clear();
            DrawData = {};
            bValid   = false;
        }

        bool IsValid() const { return bValid; }
        ImDrawData* GetDrawData() { return bValid ? &DrawData : nullptr; }

    private:

        ImDrawData              DrawData = {};
        TVector<ImDrawList*>    ClonedCmdLists;
        bool                    bValid = false;
    };
}
