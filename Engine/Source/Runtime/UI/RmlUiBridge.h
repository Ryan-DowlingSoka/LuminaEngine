#pragma once

// One Rml::Context per CWorld. Input flows from FInputViewport via GetContextForWorld.

#include <glm/glm.hpp>
#include "Containers/String.h"

namespace Rml
{
    class Context;
}

namespace Lumina
{
    class CWorld;
    class ICommandList;
    class FRHIImage;
    class FRmlUiRenderer;
}

namespace Lumina::Lua
{
    class FRef;
}

namespace Lumina::RmlUi
{
    RUNTIME_API bool            Initialise();
    RUNTIME_API void            Shutdown();

    RUNTIME_API void            OnWorldInitialized(CWorld* World);
    RUNTIME_API void            OnWorldTornDown(CWorld* World);

    RUNTIME_API void            TickAll();
    RUNTIME_API void            RenderAll(ICommandList& CmdList);

    RUNTIME_API Rml::Context*   GetActiveContext();
    RUNTIME_API Rml::Context*   GetContextForWorld(CWorld* World);

    RUNTIME_API FRmlUiRenderer* GetRenderer();

    // Editor-only preview contexts. Lifetime is owned by the caller via the
    // returned Rml::Context*; tick/render integration is automatic — TickAll
    // calls Update() each frame and RenderAll renders to the registered target
    // image. Set Target to nullptr to skip rendering for a frame (e.g. while
    // the preview window is hidden or being resized).
    RUNTIME_API Rml::Context*   CreateEditorContext(const char* Name, const glm::uvec2& InitialSize);
    RUNTIME_API void            DestroyEditorContext(Rml::Context* Context);
    RUNTIME_API void            SetEditorContextTarget(Rml::Context* Context, FRHIImage* Target, const glm::uvec2& Size);

    // Override the density-independent pixel ratio for an editor context.
    // Increasing this scales up text + dp-sized layout in the preview without
    // touching the source RML. Editor contexts skip the world's auto height-
    // based DPI heuristic entirely.
    RUNTIME_API void            SetEditorContextDpiScale(Rml::Context* Context, float Scale);

    // Color the RT is cleared to before each preview frame. Alpha is honoured
    // (RT is RGBA8) so a 0-alpha clear lets the editor compose its own
    // background (checker, color, image) behind the preview in ImGui.
    RUNTIME_API void            SetEditorContextClearColor(Rml::Context* Context, const glm::vec4& Color);

    // Replaces whatever document is currently shown in the preview context
    // with one parsed from the supplied buffer. SourceUrl is used by RmlUi
    // to resolve relative includes (RCSS <link>, image src=...). Returns
    // false if parsing failed; the previous document is unloaded either way.
    RUNTIME_API bool            ReplaceEditorContextDocument(Rml::Context* Context, FStringView Body, FStringView SourceUrl);
    RUNTIME_API void            ClearEditorContextDocument(Rml::Context* Context);

    RUNTIME_API void            RegisterLuaModule(Lua::FRef& GlobalsRef);
}
