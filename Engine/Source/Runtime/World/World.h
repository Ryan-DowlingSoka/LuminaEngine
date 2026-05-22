#pragma once

#include "Core/Object/Object.h"
#include "Core/UpdateContext.h"
#include "Core/Delegates/Delegate.h"
#include "World/Entity/Components/CameraComponent.h"
#include "Entity/Registry/EntityRegistry.h"
#include "Renderer/RHIFwd.h"
#include "Memory/SmartPtr.h"
#include "Physics/PhysicsScene.h"
#include "Entity/Systems/SystemContext.h"
#include "Scene/RenderScene/RenderScene.h"
#include "UI/WorldUIContext.h"
#include "Subsystems/FCameraManager.h"
#include "Subsystems/TimerManager.h"
#include "Physics/Ray/RayCast.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "WorldTypes.h"
#include "Core/Functional/FunctionRef.h"
#include "Entity/Systems/EntitySystem.h"
#include "Entity/Events/LuaEventBus.h"
#include "Scripting/Lua/Reference.h"
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

        // Steps physics. Runs on the physics worker; pair with DispatchPhysicsEvents on the game thread post-join.
        void TickPhysics();

        // Game-thread drain of physics events (Lua + entt::dispatcher).
        void DispatchPhysicsEvents();

        /**
         * Game thread: read ECS to compute camera, resolve post-process volumes,
         * and populate the scene's per-frame state. Must run before any render
         * thread call to Render() consumes that state. Mutates scene members.
         */
        void Extract();

        /** Render thread: emit the scene's draw commands from FrameIndex's snapshot. */
        void Render(ICommandList& CmdList, uint8 FrameIndex) const;
        
        FUNCTION(Script)
        entt::entity ConstructEntity(const FName& Name, const FTransform& Transform = FTransform());

        FUNCTION(Script)
        entt::entity SpawnPrefab(const FName& Path);
        
        FUNCTION(Script)
        void SpawnPrefabAsync(const FName& Path, const TFunction<void(entt::entity)>& Callback);
        
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
        
        void DuplicateEntity(entt::entity& To, entt::entity From, const TFunctionRef<bool(entt::type_info)>& Callback);
        
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

        // Frees the render scene of a world that has stayed suspended longer than
        // GraceSeconds. Stamps the suspend time on first idle observation. Returns
        // true when it actually reclaimed (so callers can budget one stall/frame).
        bool ReclaimIdleRenderer(double NowSeconds, double GraceSeconds);

        bool IsSimulating() const { return WorldType == EWorldType::Simulation; }

        static CWorld* DuplicateWorld(CWorld* OwningWorld);

        IRenderScene* GetRenderer() const { return RenderScene.get(); }

        // Per-world UI (Rml context + documents); created in InitializeWorld, freed in TeardownWorld.
        FWorldUIContext* GetUIContext() const { return UIContext.get(); }

        const TVector<FSystemVariant>& GetSystemsForUpdateStage(EUpdateStage Stage);

        void OnRelationshipComponentDestroyed(entt::registry& Registry, entt::entity Entity);
        void OnTransformComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void OnScriptComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void SetupScriptComponent(entt::entity Entity, SScriptComponent& ScriptComponent);
        void OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady);
        void OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity);

        // Hot-reload entry point: drop the existing script binding on this component
        // and run the OnScriptComponentCreated bind path again with the freshly
        // compiled FScript. Called by the FScriptingContext::OnScriptLoaded
        // multicast when an editor save reloads the source on disk.
        void ReloadScriptForComponent(entt::entity Entity, SScriptComponent& ScriptComponent);

        // Walks every script component whose ScriptPath matches Path and re-binds
        // it. Wired to FScriptingContext::OnScriptLoaded in InitializeWorld.
        void OnScriptSourceReloaded(FStringView Path);

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

        // Drives OnFixedUpdate (game/simulation worlds, at the physics fixed rate) and
        // OnEditorUpdate (editor worlds, once per frame) on the game thread.
        void TickFixedUpdate();
        void TickEditorUpdate();

        // Whether a script's lifecycle (OnAttach/OnReady/OnDetach) should run in this
        // world. Editor worlds run only scripts that define OnEditorUpdate; every other
        // world type runs all scripts.
        bool IsScriptActiveInWorld(entt::entity Entity) const;
    
    private:
        
        FEntityRegistry                                     RegistryPending;
        FEntityRegistry                                     EntityRegistry;
        entt::dispatcher                                    SingletonDispatcher;
        entt::entity                                        SingletonEntity;

        FSystemContext                                      SystemContext;
        
        TUniquePtr<FCameraManager>                          CameraManager;
        TUniquePtr<IRenderScene>                            RenderScene;
        TUniquePtr<Physics::IPhysicsScene>                  PhysicsScene;
        TUniquePtr<FWorldUIContext>                         UIContext;
        
        TVector<FSystemVariant>                             SystemUpdateList[(int32)EUpdateStage::Max];

        FLuaEventBus                                        LuaEventBus;
        FTimerManager                                       TimerManager;

        // Subscription to FScriptingContext::OnScriptLoaded — populated in
        // InitializeWorld, removed in TeardownWorld. The handler walks every
        // SScriptComponent matching the reloaded path and re-binds it.
        FDelegateHandle                                     ScriptReloadedHandle;

        FLineBatcherComponent*                              LineBatcherComponent;

        // World-scoped Lua bindings hoisted off the per-script setup path.
        // The DrawInterface table holds debug-draw functions captured against
        // this CWorld; the ref is built once in InitializeWorld and assigned
        // by reference to each new SScriptComponent's environment so per-entity
        // setup avoids allocating a fresh table + C closure.
        Lua::FRef                                           DrawInterfaceRef;

        FWorldContext*                                      OwningContext = nullptr;
        double                                              DeltaTime = 0.0;
        double                                              TimeSinceCreation = 0.0;

        // Time (engine clock) this world last went suspended; -1 while active or
        // not yet stamped. Drives the idle render-scene reclaim grace window.
        double                                              SuspendedTime = -1.0;

        // Game-thread accumulator driving OnFixedUpdate at the physics fixed rate.
        float                                               FixedUpdateAccumulator = 0.0f;
        
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

