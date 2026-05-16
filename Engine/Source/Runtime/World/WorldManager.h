#pragma once

#include "World.h"
#include "WorldContext.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    class RUNTIME_API FWorldManager
    {
    public:

        FWorldManager() = default;
        ~FWorldManager();
        LE_NO_COPYMOVE(FWorldManager);

        void UpdateWorlds(const FUpdateContext& UpdateContext);

        // Game thread: run CWorld::Extract on every active world. Caller must
        // first ensure the prior frame's RenderWorlds has finished or it races
        // on scene-thread state.
        void ExtractWorlds();

        // Render thread: emit each world's draws onto CmdList. Reads only scene
        // state populated by the matching ExtractWorlds.
        void RenderWorlds(ICommandList& CmdList);

        // Creates a context for an already-constructed CWorld and calls InitializeWorld on it.
        // Returns the owning context (stable pointer until DestroyWorldContext).
        FWorldContext* CreateWorldContext(CWorld* World, EWorldType Type, ENetMode NetMode = ENetMode::Standalone);

        // Tears the world down and removes its context. Safe to call with a world that has no context.
        void DestroyWorldContext(CWorld* World);

        // Context lookup. Returns nullptr if the world isn't registered.
        FWorldContext* FindContext(CWorld* World);

        // First Game-type context encountered; null if no game world is loaded (eg, editor-only session).
        FWorldContext* GetPrimaryGameContext();

        // Duplicates SourceWorld, registers a PIE context of the given type, returns the new world.
        // SessionType should be EWorldType::Game (Play-In-Editor) or EWorldType::Simulation (Simulate).
        CWorld* StartPIE(CWorld* SourceWorld, EWorldType SessionType, ENetMode NetMode = ENetMode::Standalone);

        // Tears down and removes a PIE context created by StartPIE.
        void StopPIE(CWorld* PIEWorld);

        const TVector<TUniquePtr<FWorldContext>>& GetContexts() const { return Contexts; }

    private:

        TVector<TUniquePtr<FWorldContext>> Contexts;
    };

    RUNTIME_API extern FWorldManager* GWorldManager;
}
