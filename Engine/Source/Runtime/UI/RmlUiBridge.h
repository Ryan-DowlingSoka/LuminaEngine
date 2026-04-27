#pragma once

// RmlUi 6.0 integration entry points.
// One Rml::Context per CWorld; the renderer/system/file interfaces are shared.
// FWorldManager calls OnWorldInitialized / OnWorldTornDown around its world
// lifecycle. The Lua API operates on the active world's context.

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

    /** Called by FWorldManager once a world is registered, before InitializeWorld
        runs (so OnReady scripts already see the world's context as active). */
    RUNTIME_API void            OnWorldInitialized(CWorld* World);

    /** Called by FWorldManager just before TeardownWorld. */
    RUNTIME_API void            OnWorldTornDown(CWorld* World);

    /** Drive Rml::Context::Update on every per-world context. */
    RUNTIME_API void            TickAll();

    /** Render every world's UI into its own render target. CmdList is the
        same one used by world rendering and ImGui. */
    RUNTIME_API void            RenderAll(ICommandList& CmdList);

    /** The active context (Lua API target). Null if no world has a context. */
    RUNTIME_API Rml::Context*   GetActiveContext();

    /** Shared renderer instance. */
    RUNTIME_API FRmlUiRenderer* GetRenderer();

    /** Editor input forwarding. Coords are context-space (world RT pixels);
        modifiers use Rml::Input::KeyModifier flags. The editor's viewport
        draw path translates panel-local mouse to context space and routes to
        the supplied World's context. */
    RUNTIME_API void ForwardMouseMove(CWorld* World, int X, int Y, int KeyModifierState = 0);
    RUNTIME_API void ForwardMouseButton(CWorld* World, int Button, bool bPressed, int KeyModifierState = 0);
    RUNTIME_API void ForwardMouseWheel(CWorld* World, float Delta, int KeyModifierState = 0);
    RUNTIME_API void ForwardMouseLeave(CWorld* World);

    /** Wires the `UI` global table on the script VM. */
    RUNTIME_API void            RegisterLuaModule(Lua::FRef& GlobalsRef);
}
