#pragma once
#include <imgui.h>
#include "Memory/Memory.h"
#include "Memory/MemoryTracking.h"

namespace Lumina::ImGuiX
{
    // ImGui is statically linked into every module that uses it (Runtime, Editor, Sandbox),
    // so each has its OWN copy of ImGui's allocator globals. ImGui objects (the font atlas,
    // ImFontBaked index buffers, window state) are allocated and freed across those module
    // boundaries -- so if only one copy is routed through rpmalloc, a buffer allocated by one
    // module gets freed by another with a different allocator and corrupts the heap (crash in
    // ImFontBaked_BuildGrowIndex).
    //
    // The fix: call InstallImGuiAllocator() once in EVERY module that links ImGui, at module
    // startup, before that module's first ImGui allocation. All copies forward to the same
    // Memory::Malloc / rpmalloc heap, so cross-module frees stay consistent. Allocations are
    // attributed to the "ImGui" category.
    inline void* ImGuiMemAlloc(size_t Size, void*)
    {
        LUMINA_MEMORY_SCOPE("ImGui");
        return Memory::Malloc(Size);
    }

    inline void ImGuiMemFree(void* Ptr, void*)
    {
        if (Ptr)
        {
            Memory::Free(Ptr);
        }
    }

    inline void InstallImGuiAllocator()
    {
        ImGui::SetAllocatorFunctions(&ImGuiMemAlloc, &ImGuiMemFree);
    }
}
