#pragma once

#include "World.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Subsystems/Subsystem.h"

namespace Lumina
{
    enum class EWorldType : uint8;
    class FDeferredRenderScene;
}

namespace Lumina
{
    class RUNTIME_API FWorldManager
    {
    public:
        
        struct FManagedWorld
        {
            EWorldType          Type;
            TObjectPtr<CWorld>  World;
        };

        FWorldManager() = default;
        ~FWorldManager();
        LE_NO_COPYMOVE(FWorldManager);
        
        void UpdateWorlds(const FUpdateContext& UpdateContext);
        void RenderWorlds(FRenderGraph& RenderGraph);

        void RemoveWorld(CWorld* World);
        void AddWorld(CWorld* World);
    
        const TVector<FManagedWorld>& GetManagedWorlds() const { return Worlds; }
    
    private:

        TWeakObjectPtr<CWorld> CurrentEditorWorld;
        TVector<FManagedWorld> Worlds;
    };
    
    RUNTIME_API extern FWorldManager* GWorldManager;
}
