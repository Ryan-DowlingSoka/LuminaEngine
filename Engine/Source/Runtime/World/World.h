#pragma once

#include "Core/Object/Object.h"
#include "Core/UpdateContext.h"
#include "World/Entity/Components/CameraComponent.h"
#include "Entity/Registry/EntityRegistry.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Memory/SmartPtr.h"
#include "Physics/PhysicsScene.h"
#include "Entity/Systems/SystemContext.h"
#include "Scene/RenderScene/RenderScene.h"
#include "Subsystems/FCameraManager.h"
#include "Subsystems/TimerManager.h"
#include "Physics/Ray/RayCast.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "WorldTypes.h"
#include "Core/Functional/FunctionRef.h"
#include "Entity/Systems/EntitySystem.h"
#include "Entity/Events/LuaEventBus.h"
#include "World.generated.h"


namespace Lumina
{
    struct FScriptComponentPendingReady;
    struct SScriptComponent;
    struct SDefaultWorldSettings;
    struct FLineBatcherComponent;
    struct FWorldContext;
    enum class ENetMode : uint8;
}

namespace Lumina
{
    REFLECT()
    class RUNTIME_API CWorld : public CObject, public IPrimitiveDrawInterface
    {
        GENERATED_BODY()
        
        friend class FWorldManager;
        friend struct FSystemContext;
        friend struct SRenderComponent;
        
    public:
        
        using FSystemVariant = TVariant<FEntitySystemWrapper, FEntityScriptSystem>;

        CWorld();
        static void RegisterLuaModule(Lua::FRef& GlobalRef);

        //~ Begin CObject Interface
        void Serialize(FArchive& Ar) override;
        void PreLoad() override;
        void PostLoad() override;
        bool IsAsset() const override { return true; }
        //~ End CObject Interface
        
        /**
         * Initializes systems and renderer. Must be called before anything is done with the world.
         */
        void InitializeWorld(EWorldType InWorldType);
        
        /**
         * Called to shut down the world, destroys system, components, and entities.
         */
        void TeardownWorld();
        
        /**
         * Called on every update stage and runs systems attached to this world.
         */
        void Update(const FUpdateContext& Context);
        void Render(FRenderGraph& RenderGraph) const;
        
        FUNCTION(Script)
        entt::entity ConstructEntity(const FName& Name, const FTransform& Transform = FTransform());
        
        FUNCTION(Script)
        FEntityRegistry& GetEntityRegistry() { return EntityRegistry; }
        
        FUNCTION(Script)
        Physics::IPhysicsScene* GetPhysicsScene() const { return PhysicsScene.get(); }
        
        FUNCTION(Script)
        STransformComponent& GetEntityTransform(entt::entity Entity);
        
        FUNCTION(Script)
        glm::vec3 GetEntityLocation(entt::entity Entity);
        
        FUNCTION(Script)
        void SetEntityLocation(entt::entity Entity, glm::vec3 Location);
        
        FUNCTION(Script)
        void SetEntityRotation(entt::entity Entity, glm::quat Rotation);
        
        FUNCTION(Script)
        glm::vec3 TranslateEntity(entt::entity Entity, glm::vec3 Translation);
        
        FUNCTION(Script)
        uint32 GetNumEntities() const;
        
        FUNCTION(Script)
        SDefaultWorldSettings& GetDefaultWorldSettings();
        
        FUNCTION(Script)
        bool EntityHasTag(entt::entity Entity, const FName& Tag);
        
        FUNCTION(Script)
        entt::entity GetEntityByTag(const FName& Tag);
        
        FUNCTION(Script)
        entt::entity GetEntityByName(const FName& Name);

        FUNCTION(Script)
        TOptional<SRayResult> CastRay(const SRayCastSettings& Settings);

        FUNCTION(Script)
        TVector<SRayResult> CastSphere(const SSphereCastSettings& Settings) const;
        
        FUNCTION(Script)
        EUpdateStage GetUpdateStage() const;

        FTimerManager& GetTimerManager() { return TimerManager; }
        const FTimerManager& GetTimerManager() const { return TimerManager; }

        NODISCARD EWorldType GetWorldType() const { return WorldType; }

        /** The context this world belongs to. Non-null once the world has been registered via FWorldManager::CreateWorldContext. */
        NODISCARD FWorldContext* GetWorldContext() const { return OwningContext; }

        /** Shorthand for GetWorldContext()->NetMode; returns Standalone when no context is set. */
        NODISCARD ENetMode GetNetMode() const;
        
        entt::entity GetFirstEntityWith(entt::id_type Type);
        
        void DuplicateEntity(entt::entity& To, entt::entity From, TFunctionRef<bool(entt::type_info)> Callback);
        
        void DestroyEntity(entt::entity Entity);
        
        void SetActiveCamera(entt::entity InEntity) const;
        
        SCameraComponent* GetActiveCamera() const;
        
        entt::entity GetActiveCameraEntity() const;
        
        void OnChangeCameraEvent(const FSwitchActiveCameraEvent& Event);
        
        double GetWorldDeltaTime() const { return DeltaTime; }
        double GetTimeSinceWorldCreation() const { return TimeSinceCreation; }
        

        void SetPaused(bool bNewPause) { bPaused = bNewPause; }
        bool IsPaused() const { return bPaused; }

        void SetActive(bool bNewActive);
        bool IsSuspended() const { return !bActive; }

        bool IsSimulating() const { return WorldType == EWorldType::Simulation; }

        static CWorld* DuplicateWorld(CWorld* OwningWorld);

        IRenderScene* GetRenderer() const { return RenderScene.get(); }

        const TVector<FSystemVariant>& GetSystemsForUpdateStage(EUpdateStage Stage);

        void OnRelationshipComponentDestroyed(entt::registry& Registry, entt::entity Entity);
        void OnTransformComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void OnScriptComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady);
        void OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity);

        void RegisterSystems();
        
        //~ Begin Debug Drawing
        void DrawBillboard(FRHIImage* Image, const glm::vec3& Location, float Scale) override;
        void DrawLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = -1.0f) override;
        //~ End Debug Drawing
        
        FORCEINLINE bool IsGameWorld() const { return WorldType == EWorldType::Game; }
        
        void SetEntityTransform(entt::entity Entity, const FTransform& NewTransform);
        TVector<entt::entity> GetSelectedEntities() const;
        bool IsSelected(entt::entity Entity) const;

        template<typename TFunc>
        void ForEachUniqueSystem(TFunc&& Func);
        
        const FSystemContext& GetSystemContext() const { return SystemContext; }
        
    private:
        
        void CreateRenderer();
        void DestroyRenderer();
        
        void OnScriptComponentPendingReady(const FScriptComponentPendingReady& Event);
        
        void InitializeScriptEntities();
        bool RegisterSystem(const FSystemVariant& NewSystem);
        void TickSystems(FSystemContext& Context);
    
    private:
        
        FEntityRegistry                                     RegistryPending;
        FEntityRegistry                                     EntityRegistry;
        entt::dispatcher                                    SingletonDispatcher;
        entt::entity                                        SingletonEntity;

        FSystemContext                                      SystemContext;
        
        TUniquePtr<FCameraManager>                          CameraManager;
        TUniquePtr<IRenderScene>                            RenderScene;
        TUniquePtr<Physics::IPhysicsScene>                  PhysicsScene;
        
        TVector<FSystemVariant>                             SystemUpdateList[(int32)EUpdateStage::Max];

        FLuaEventBus                                        LuaEventBus;
        FTimerManager                                       TimerManager;

        FLineBatcherComponent*                              LineBatcherComponent;
        
        FWorldContext*                                      OwningContext = nullptr;
        double                                              DeltaTime = 0.0;
        double                                              TimeSinceCreation = 0.0;
        
        uint32                                              bPaused:1 = true;
        uint32                                              bActive:1 = true;
        
        
        EWorldType                                          WorldType = EWorldType::None;
        bool                                                bInitializing = true;
    };
    
    
    
    
    
    
    
    
    
    

    template <typename TFunc>
    void CWorld::ForEachUniqueSystem(TFunc&& Func)
    {
        THashSet<uint64> UniqueSystems;
        for (auto& i : SystemUpdateList)
        {
            for (const FSystemVariant& System : i)
            {
                uint64 Hash = eastl::visit([&](const auto& Sys) { return Sys.GetHash(); }, System);
                if (UniqueSystems.count(Hash) == 0)
                {
                    Func(System);
                    UniqueSystems.emplace(Hash);
                }
            }
        }
    }
}

