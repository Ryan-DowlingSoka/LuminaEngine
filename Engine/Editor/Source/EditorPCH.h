#pragma once

// Editor PCH. Includes Runtime's PCH (which carries STL/EASTL/glm/entt/xxhash)
// plus the editor-only heavies (ImGui).
//
// Keep this lean: anything pulled in here is paid by all 94 Editor TUs every
// build. Editor-specific headers (EditorUI, EditorTool, etc.) intentionally
// stay out so that touching one of them doesn't dirty the entire PCH.

#include "pch.h"

// ImGui is touched by ~25 Editor TUs directly and far more transitively.
#include <imgui.h>
