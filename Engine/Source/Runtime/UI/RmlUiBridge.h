#pragma once

// Process-global RmlUi backend. The per-world Rml::Context lives on CWorld
// (FWorldUIContext); this bridge owns only the shared interfaces/renderer/font/
// debugger plus editor-only preview contexts. Input flows from FInputViewport
// via FLockedWorldContext.

#include <glm/glm.hpp>
#include "Containers/String.h"
#include "Memory/SmartPtr.h"

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
    struct FWorldUIContext;
    struct SWidgetComponent;
}

namespace Lumina::Lua
{
    class FRef;
}

namespace Lumina::RmlUi
{
    RUNTIME_API bool            Initialize();
    RUNTIME_API void            Shutdown();

    // Per-world context lifecycle. CWorld owns the returned wrapper; CreateWorldUI
    // builds the Rml::Context, DestroyWorldUI removes it. No external bookkeeping.
    RUNTIME_API TUniquePtr<FWorldUIContext> CreateWorldUI(CWorld* World);
    RUNTIME_API void            DestroyWorldUI(CWorld* World);

    // Game thread: update one world's DOM (called from CWorld::Extract).
    RUNTIME_API void            TickWorldUI(CWorld* World);
    // Render thread: composite one world's UI onto its render target (from CWorld::Render).
    RUNTIME_API void            RenderWorldUI(const CWorld* World, ICommandList& CmdList);

    // World-space widgets (SWidgetComponent). Per-widget state lives on the component
    // (SWidgetComponent::Runtime). TickWorldWidgets iterates the world's view, lays each
    // document into its offscreen RT, and queues render jobs (game thread, from CWorld::Extract
    // before the render gather). RenderWorldWidgets rasterizes those queued RTs (render thread,
    // from CWorld::Render before the scene). ReleaseWidget tears down one widget's context + RT,
    // called from the world's on_destroy<SWidgetComponent> hook.
    RUNTIME_API void            TickWorldWidgets(CWorld* World);
    RUNTIME_API void            RenderWorldWidgets(const CWorld* World, ICommandList& CmdList);
    RUNTIME_API void            ReleaseWidget(CWorld* World, SWidgetComponent& Component);

    // The world whose context the `UI.*` Lua module targets. Set when a world comes
    // up or resumes; cleared when it tears down.
    RUNTIME_API void            SetActiveWorld(CWorld* World);

    // Editor-only preview contexts (not bound to any world); ticked/rendered here.
    RUNTIME_API void            TickEditorContexts();
    RUNTIME_API void            RenderEditorContexts(ICommandList& CmdList);

    RUNTIME_API Rml::Context*   GetContextForWorld(CWorld* World);

    // True when the cursor is over an interactive element in this world's UI, so
    // editor picking / marquee should yield to it. Reads RmlUi's current hover.
    RUNTIME_API bool            WorldUIWantsMouse(const CWorld* World);

    // RAII handle: acquires the bridge state lock for its lifetime and resolves
    // the Rml::Context for a world. Use this whenever you need to call into the
    // context (input dispatch via ProcessMouseMove/ProcessKeyDown/etc.) — the
    // lock blocks the render thread's RenderWorldUI from walking the DOM until you
    // release, and keeps the context pointer valid against teardown.
    class RUNTIME_API FLockedWorldContext
    {
    public:
        explicit FLockedWorldContext(CWorld* World);
        ~FLockedWorldContext();

        FLockedWorldContext(const FLockedWorldContext&)            = delete;
        FLockedWorldContext& operator=(const FLockedWorldContext&) = delete;

        Rml::Context* Get() const { return Context; }
        Rml::Context* operator->() const { return Context; }
        explicit operator bool() const { return Context != nullptr; }

    private:
        Rml::Context* Context = nullptr;
        bool          bLocked = false;
    };

    // Lay UI out at this size instead of the RT image size; {0,0} reverts. Used by the editor viewport.
    RUNTIME_API void            SetWorldDisplaySize(CWorld* World, const glm::uvec2& Size);

    // Replace a world context's documents with one parsed from an in-memory RML
    // body (e.g. the material editor's UI-material preview). SourceUrl resolves
    // relative includes. Returns false on parse failure.
    RUNTIME_API bool            SetWorldInlineDocument(CWorld* World, FStringView Body, FStringView SourceUrl);

    RUNTIME_API FRmlUiRenderer* GetRenderer();

    // Editor preview contexts. Caller owns lifetime; pass Target=nullptr to skip a frame.
    RUNTIME_API Rml::Context*   CreateEditorContext(const char* Name, const glm::uvec2& InitialSize);
    RUNTIME_API void            DestroyEditorContext(Rml::Context* Context);
    RUNTIME_API void            SetEditorContextTarget(Rml::Context* Context, FRHIImage* Target, const glm::uvec2& Size);

    /** Editor contexts skip the world auto-DPI heuristic. */
    RUNTIME_API void            SetEditorContextDpiScale(Rml::Context* Context, float Scale);

    /** RGBA8; 0-alpha clear allows the editor to composite its own background. */
    RUNTIME_API void            SetEditorContextClearColor(Rml::Context* Context, const glm::vec4& Color);

    /** SourceUrl resolves relative includes. Previous document unloads either way; returns false on parse failure. */
    RUNTIME_API bool            ReplaceEditorContextDocument(Rml::Context* Context, FStringView Body, FStringView SourceUrl);
    RUNTIME_API void            ClearEditorContextDocument(Rml::Context* Context);

    RUNTIME_API void            RegisterLuaModule(Lua::FRef& GlobalsRef);
}
