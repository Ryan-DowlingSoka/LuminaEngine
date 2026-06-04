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
#include "Scene/RenderScene/TexturePaintTypes.h"
#include "UI/WorldUIContext.h"
#include "Subsystems/TimerManager.h"
#include "Physics/Ray/RayCast.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "WorldTypes.h"
#include "Core/Functional/FunctionRef.h"
#include "Entity/Systems/EntitySystem.h"
#include "Entity/Events/LuaEventBus.h"
#include "Scripting/Lua/Reference.h"
#include "World/Net/NetLuaInterface.h"
#include "World.generated.h"


namespace Lumina
{
    struct FScriptComponentPendingReady;
    struct SScriptComponent;
    struct SDefaultWorldSettings;
    struct FLineBatcherComponent;
    struct FTriangleBatcherComponent;
    struct FSimpleElementVertex;
    struct FWorldContext;
    class CTexture;
    class CTextureRenderTarget;
    class CWorld;
    enum class ENetMode : uint8;
}

namespace Lumina
{
    // One queued screen-space debug-text line (DrawDebugText). Drained by the render scene each frame.
    struct FDebugTextLine
    {
        FString  Text;
        FVector4 Color = FVector4(1.0f);
    };

    // Lua-facing facade for the World.Debug namespace. One instance lives on each CWorld and is reached
    // as World.Debug (registered via RegisterWorldLuaSubsystem, exactly like World.Net). Screen-space
    // debug text + world debug shapes; trailing args (thickness, depth-test, duration) are optional.
    // Dev/Debug only -- the draws are no-ops in Shipping. Methods forward to this world's draw interface.
    struct FWorldDebugInterface
    {
        CWorld* World = nullptr;

        void DrawText(FStringView Text, TOptional<FVector4> Color);
        void DrawLine(FVector3 Start, FVector3 End, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration);
        void DrawBox(FVector3 Center, FVector3 HalfExtents, FQuat Rotation, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration);
        void DrawSphere(FVector3 Center, float Radius, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration);
        void DrawCapsule(FVector3 Start, FVector3 End, float Radius, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration);
        void DrawCone(FVector3 Apex, FVector3 Direction, float AngleRadians, float Length, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration);
        void DrawArrow(FVector3 Start, FVector3 Direction, float Length, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration);
    };

    REFLECT()
    class RUNTIME_API CWorld : public CObject, public IPrimitiveDrawInterface
    {
        GENERATED_BODY()
        
        friend class FWorldManager;
        friend struct FSystemContext;
        friend struct SRenderComponent;
        
    public:
        
        using FSystemVariant = TVariant<FEntitySystemWrapper, FEntityScriptSystem>;

        // A system as scheduled in a stage: its variant, the priority for that stage, and its declared
        // component/resource access (used to run non-conflicting systems concurrently in TickSystems).
        struct FScheduledSystem
        {
            FSystemVariant Variant;
            FSystemAccess  Access;
            uint8          StagePriority = 255;
        };

        CWorld();
        static void RegisterLuaModule(Lua::FRef& GlobalRef);

        //~ Begin CObject Interface
        void Serialize(FArchive& Ar) override;
        void PreLoad() override;
        void PostLoad() override;
        bool IsAsset() const override { return true; }
        //~ End CObject Interface
        
        /** Initializes systems and renderer. Must be called before anything is done with the world. */
        void InitializeWorld(EWorldType InWorldType);

        /** Shuts down the world; destroys systems, components, and entities. */
        void TeardownWorld();

        /** Runs systems attached to this world; called on every update stage. */
        void Update(const FUpdateContext& Context);

        // Steps physics. Runs on the physics worker; pair with DispatchPhysicsEvents on the game thread post-join.
        void TickPhysics();

        // Game-thread drain of physics events (Lua + entt::dispatcher).
        void DispatchPhysicsEvents();

        // Game thread: read ECS to compute camera/post-process and populate the scene's
        // per-frame state. Must run before any render-thread Render() consumes it.
        void Extract();

        /** Render thread: emit the scene's draw commands from FrameIndex's snapshot. */
        void Render(ICommandList& CmdList, uint8 FrameIndex) const;
        
        FUNCTION(Script)
        entt::entity ConstructEntity(const FName& Name, const FTransform& Transform = FTransform());

        FUNCTION(Script)
        entt::entity SpawnPrefab(const FName& Path);

        /** Like SpawnPrefab(Path), but positions the spawned root at SpawnTransform and
         *  optionally reparents under Parent (entt::null = world root). */
        FUNCTION(Script)
        entt::entity SpawnPrefabAt(const FName& Path, const FTransform& SpawnTransform, entt::entity Parent = entt::null);

        // Shatter a destructible entity into physics-driven fragments. Origin = blast point;
        // Strength = outward launch m/s (0 uses ExplosionStrength). No-op without an unbroken SDestructibleComponent.
        FUNCTION(Script)
        bool FractureEntity(entt::entity Entity, const FVector3& Origin, float Strength = 0.0f);
        
        FUNCTION(Script)
        void SpawnPrefabAsync(const FName& Path, const TFunction<void(entt::entity)>& Callback);
        
        FUNCTION(Script)
        FEntityRegistry& GetEntityRegistry() { return EntityRegistry; }
        
        FUNCTION(Script)
        Physics::IPhysicsScene* GetPhysicsScene() const { return PhysicsScene.get(); }
        
        FUNCTION(Script)
        STransformComponent& GetEntityTransform(entt::entity Entity);
        
        FUNCTION(Script)
        FVector3 GetEntityLocation(entt::entity Entity);
        
        FUNCTION(Script)
        void SetEntityLocation(entt::entity Entity, FVector3 Location);
        
        FUNCTION(Script)
        void SetEntityRotation(entt::entity Entity, FQuat Rotation);
        
        FUNCTION(Script)
        FVector3 TranslateEntity(entt::entity Entity, FVector3 Translation);
        
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

        FTimerManager& GetTimerManager() { return EntityRegistry.ctx().get<FTimerManager>(); }
        const FTimerManager& GetTimerManager() const { return EntityRegistry.ctx().get<FTimerManager>(); }

        NODISCARD EWorldType GetWorldType() const { return WorldType; }

        /** The context this world belongs to. Non-null once the world has been registered via FWorldManager::CreateWorldContext. */
        NODISCARD FWorldContext* GetWorldContext() const { return OwningContext; }

        /** Shorthand for GetWorldContext()->NetMode; returns Standalone when no context is set. */
        NODISCARD ENetMode GetNetMode() const;

        /** Lua-facing net query facade (World.Net). */
        NODISCARD FNetLuaInterface* GetNetInterface() { return &NetInterface; }

        /** Lua-facing debug-draw facade (World.Debug). */
        NODISCARD FWorldDebugInterface* GetDebugInterface() { return &DebugInterface; }
        
        entt::entity GetFirstEntityWith(entt::id_type Type);
        
        void DuplicateEntity(entt::entity& To, entt::entity From, const TFunctionRef<bool(entt::type_info)>& Callback);
        
        void DestroyEntity(entt::entity Entity);
        
        void SetActiveCamera(entt::entity InEntity) const;

        /** Switch the active camera, easing from the current view over BlendTime seconds (0 = snap). */
        void SetActiveCamera(entt::entity InEntity, float BlendTime, ECameraBlendFunction Function = ECameraBlendFunction::EaseInOut) const;

        SCameraComponent* GetActiveCamera() const;
        
        entt::entity GetActiveCameraEntity() const;
        
        void OnChangeCameraEvent(const FSwitchActiveCameraEvent& Event);
        
        double GetWorldDeltaTime() const { return DeltaTime; }
        double GetTimeSinceWorldCreation() const { return TimeSinceCreation; }
        

        void SetPaused(bool bNewPause) { bPaused = bNewPause; }
        bool IsPaused() const { return bPaused; }

        void SetActive(bool bNewActive);
        bool IsSuspended() const { return !bActive; }

        // Frees the render scene once suspended longer than GraceSeconds. Returns true when it
        // actually reclaimed, so callers can budget one stall/frame.
        bool ReclaimIdleRenderer(double NowSeconds, double GraceSeconds);

        bool IsSimulating() const { return WorldType == EWorldType::Simulation; }

        static CWorld* DuplicateWorld(CWorld* OwningWorld);

        IRenderScene* GetRenderer() const { return RenderScene.get(); }

        // A world renders only when the process has a real RHI (not headless) and the world isn't a
        // dedicated server (which is invisible even in the editor). Gates RenderScene creation.
        NODISCARD bool ShouldRender() const;

        // Per-world UI (Rml context + documents); created in InitializeWorld, freed in TeardownWorld.
        FWorldUIContext* GetUIContext() const { return UIContext.get(); }

        const TVector<FScheduledSystem>& GetSystemsForUpdateStage(EUpdateStage Stage);

        // One reflected engine system, as surfaced to the World Editor's Systems panel.
        struct FSystemInfo
        {
            FName                   Name;
            bool                    bEnabled = true;
            TVector<EUpdateStage>   Stages;     // stages this system participates in
        };

        // Enumerate every reflected engine system (alphabetical by reflected name) plus whether it is
        // currently enabled for this world. Reflects the pending (intended) state, so a UI checkbox
        // updates instantly even though the actual system list rebuild is deferred to the next frame.
        void GetAllSystems(TVector<FSystemInfo>& Out) const;

        // Whether System (by reflected name) is enabled for this world (reads the pending state).
        bool IsSystemEnabled(FName System) const;

        // Enable/disable a system for this world. Persists to SDefaultWorldSettings immediately and
        // defers the live system-list rebuild to the start of the next frame (ApplyPendingSystemChanges),
        // so it is safe to call mid-frame.
        void SetSystemEnabled(FName System, bool bEnabled);

        void OnRelationshipComponentDestroyed(entt::registry& Registry, entt::entity Entity);
        void OnTransformComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void OnScriptComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void OnScriptComponentUpdated(entt::registry& Registry, entt::entity Entity);
        void SetupScriptComponent(entt::entity Entity, SScriptComponent& ScriptComponent);
        void OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady);
        void OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity);
        void OnWidgetComponentDestroyed(entt::registry& Registry, entt::entity Entity);
        void OnInputComponentConstruct(entt::registry& Registry, entt::entity Entity);
        void OnInputComponentDestroyed(entt::registry& Registry, entt::entity Entity);

        // Clean (re)bind: drop this component's current script binding (detach + clear FRefs/tags/subs) and
        // re-run OnScriptComponentCreated from the component's CURRENT ScriptPath. Used by hot-reload AND by
        // SetEntityScript / replicated script swaps. Safe whether or not a script is currently attached.
        void ReloadScriptForComponent(entt::entity Entity, SScriptComponent& ScriptComponent);

        // Set (or change) the script on an entity by path: emplaces SScriptComponent if needed, sets the
        // ScriptPath, and cleanly (re)attaches via ReloadScriptForComponent (detaching any prior script). The
        // single clean entry point for switching an entity's script at runtime. On a server, MarkDirty the
        // entity afterward to replicate the change to clients.
        void SetEntityScript(entt::entity Entity, FStringView Path);

        // Re-binds every script component whose ScriptPath matches Path. Wired to OnScriptLoaded.
        void OnScriptSourceReloaded(FStringView Path);

        void RegisterSystems();
        
        //~ Begin Debug Drawing
        void DrawBillboard(FRHIImage* Image, const FVector3& Location, float Scale) override;
        void DrawLine(const FVector3& Start, const FVector3& End, const FVector4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = -1.0f) override;

        /** Submit a solid translucent triangle batch (3 pre-colored verts per tri). Duration <= 0 draws one frame. */
        void DrawSolidTriangles(TVector<FSimpleElementVertex>&& Vertices, bool bDepthTest = true, float Duration = -1.0f);

        /** Queue a line of screen-space debug text for this frame, stacked top-left on the world viewport
         *  (like Unreal's AddOnScreenDebugMessage). Dev/Debug only -- a no-op in Shipping. */
        void DrawDebugText(const FString& Text, const FVector4& Color = FVector4(1.0f));

        /** Render scene drains the queued debug-text lines each frame (moves them out + clears). */
        void DrainDebugTextLines(TVector<FDebugTextLine>& Out);
        //~ End Debug Drawing

        //~ Begin Render Target Painting
        // Stamp a soft radial brush of Color into Target at UV (0..1). RadiusUV is relative to the
        // longer side; Strength = center opacity; Hardness > 1 sharpens. Queued, run next frame (TexturePaintPass).
        void PaintRenderTarget(CTextureRenderTarget* Target, const FVector2& UV, float RadiusUV, const FVector4& Color, float Strength = 1.0f, float Hardness = 1.0f, CTexture* BrushMask = nullptr);

        /** Clear an entire render target to Color (queued; executed on the render thread). */
        void ClearRenderTarget(CTextureRenderTarget* Target, const FVector4& Color);

        /** Render-scene Extract drains the queued paint/clear ops into the frame snapshot. */
        void DrainRenderTargetPaints(TVector<FTexturePaintOp>& OutOps);
        //~ End Render Target Painting
        
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

        // Applies a deferred enable/disable request (set via SetSystemEnabled): tears down newly-disabled
        // systems, rebuilds the stage lists honoring DisabledSystems, then starts up newly-enabled ones.
        // Called at the top of Update() so it never runs inside a system batch. No-op unless bSystemsDirty.
        void ApplyPendingSystemChanges();

        // Whether a script's lifecycle runs in this world. Editor worlds run only scripts defining
        // OnEditorUpdate; every other world type runs all scripts.
        bool IsScriptActiveInWorld(entt::entity Entity) const;
    
    private:
        
        FEntityRegistry                                     RegistryPending;
        FEntityRegistry                                     EntityRegistry;
        entt::dispatcher                                    SingletonDispatcher;
        entt::entity                                        SingletonEntity;

        FSystemContext                                      SystemContext;
        
        TUniquePtr<IRenderScene>                            RenderScene;
        TUniquePtr<Physics::IPhysicsScene>                  PhysicsScene;
        TUniquePtr<FWorldUIContext>                         UIContext;
        
        TVector<FScheduledSystem>                           SystemUpdateList[(int32)EUpdateStage::Max];

        // Reflected systems disabled for this world, by name. DisabledSystems is the applied state used by
        // RegisterSystems; PendingDisabledSystems is the editor-requested next state. They diverge only
        // between a SetSystemEnabled call and the next ApplyPendingSystemChanges (which reconciles them).
        THashSet<FName>                                     DisabledSystems;
        THashSet<FName>                                     PendingDisabledSystems;
        bool                                                bSystemsDirty = false;

        // Subscription to FScriptingContext::OnScriptLoaded; re-binds matching SScriptComponents
        // on reload. Populated in InitializeWorld, removed in TeardownWorld.
        FDelegateHandle                                     ScriptReloadedHandle;

        FLineBatcherComponent*                              LineBatcherComponent;
        FTriangleBatcherComponent*                          TriangleBatcherComponent;

        // Screen-space debug-text lines queued this frame (DrawDebugText); drained by the render scene.
        TVector<FDebugTextLine>                             DebugTextLines;

        // Render-target paint/clear requests; drained each Extract into the frame snapshot.
        TConcurrentQueue<FTexturePaintOp>                   RenderTargetPaintQueue;

        // Debug-draw table captured against this CWorld, built once in InitializeWorld and shared by
        // reference into each SScriptComponent's environment to avoid a per-entity table + C closure.
        Lua::FRef                                           DrawInterfaceRef;

        FWorldContext*                                      OwningContext = nullptr;

        // Lua-facing facades bound under World.Net / World.Debug; .World points back at this world.
        FNetLuaInterface                                    NetInterface;
        FWorldDebugInterface                                DebugInterface;
        double                                              DeltaTime = 0.0;
        double                                              TimeSinceCreation = 0.0;

        // Engine-clock time this world last went suspended; -1 while active. Drives idle-reclaim grace.
        double                                              SuspendedTime = -1.0;

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
            for (const FScheduledSystem& Scheduled : i)
            {
                const FSystemVariant& System = Scheduled.Variant;
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

