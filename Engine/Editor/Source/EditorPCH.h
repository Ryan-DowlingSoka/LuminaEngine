#pragma once

// Editor PCH. Includes Runtime's PCH (which carries STL/EASTL/glm/entt/xxhash)
// plus the editor-only heavies (ImGui).
//
// Keep this lean: anything pulled in here is paid by all 95 Editor TUs every
// build, and any edit invalidates the PCH. Editor-specific headers
// (EditorUI, EditorTool, etc.) intentionally stay out so that touching one
// of them doesn't dirty the entire PCH.

// ModuleAPI defines RUNTIME_API / EDITOR_API. It's /FI-included by the build
// AFTER the PCH header, so PCH content that depends on those macros (e.g.
// any Runtime header marking RUNTIME_API on a class) needs it explicitly
// up-front here, otherwise the macro is undefined while the PCH is parsed.
#include "ModuleAPI.h"
#include "pch.h"

// ImGui is touched by ~25 Editor TUs directly and far more transitively.
#include <imgui.h>
#include <imgui_internal.h>

// ImGuiX is included by 29+ Editor TUs and transitively pulls imgui_internal,
// ImGuizmo, AssetRegistry, glm, and a handful of Containers. Hoisting it
// into the PCH replaces ~29 redundant parses with one.
#include "Tools/UI/ImGui/ImGuiX.h"
