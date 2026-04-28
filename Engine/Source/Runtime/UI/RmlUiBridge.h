#pragma once

// One Rml::Context per CWorld. Input flows from FInputViewport via GetContextForWorld.

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

    RUNTIME_API void            RegisterLuaModule(Lua::FRef& GlobalsRef);
}
