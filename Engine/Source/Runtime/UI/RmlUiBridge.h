#pragma once

// One Rml::Context per CWorld; input flows from FInputViewport via FLockedWorldContext.

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

    // RAII handle: acquires the bridge state lock for its lifetime and resolves
    // the Rml::Context for a world. Use this whenever you need to call into the
    // context (input dispatch via ProcessMouseMove/ProcessKeyDown/etc.) — the
    // lock blocks the render thread's RenderAll from walking the DOM until you
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
