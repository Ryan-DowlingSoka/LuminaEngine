#include "pch.h"
#include "World.h"
#include <cmath>
#include <utility>
#include "lua.h"
#include "WorldManager.h"
#include "WorldContext.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/GeometryCollection/GeometryCollection.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Assets/AssetTypes/Prefabs/Prefab.h"
#include "Assets/AssetTypes/Textures/TextureRenderTarget.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Object/Cast.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Object/Package/Package.h"
#include "Audio/AudioGlobals.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Engine/Engine.h"
#include "Core/Profiler/CPUProfiler.h"
#include "TaskSystem/TaskSystem.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Serialization/MemoryArchiver.h"
#include "Core/Serialization/ObjectArchiver.h"
#include "EASTL/sort.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Entity/EntityUtils.h"
#include "Entity/RuntimeComponent.h"
#include "Entity/Components/CameraComponent.h"
#include "Entity/Components/DestructibleComponent.h"
#include "Entity/Components/DirtyComponent.h"
#include "Entity/Components/EditorComponent.h"
#include "Entity/Components/LifetimeComponent.h"
#include "Entity/Components/StaticMeshComponent.h"
#include "Entity/Events/ImpulseEvent.h"
#include "entity/components/entitytags.h"
#include "Entity/Components/LineBatcherComponent.h"
#include "Entity/Components/TriangleBatcherComponent.h"
#include "Entity/Components/NameComponent.h"
#include "Entity/Components/PhysicsComponent.h"
#include "Entity/Components/PostProcessComponent.h"
#include "Entity/Components/TransformComponent.h"
#include "Entity/Components/ScriptComponent.h"
#include "Entity/Components/WidgetComponent.h"
#include "Entity/Components/InputComponent.h"
#include "Input/InputContext.h"
#include "Input/InputViewport.h"
#include "Entity/Components/SingletonEntityComponent.h"
#include "Entity/Systems/SystemSingletons.h"
#include "Entity/Systems/CameraSystem.h"
#include "entity/components/tagcomponent.h"
#include "Entity/Events/WorldEvents.h"
#include "Entity/Events/LuaEventBus.h"
#include "Physics/Physics.h"
#include "Renderer/RenderThread.h"
#include "Scene/RenderScene/Forward/ForwardRenderScene.h"
#include "Scripting/Lua/Scripting.h"
#include "World/Net/NetRpc.h"
#include "World/Net/NetWorldState.h"
#include "World/Net/NetRole.h"
#include "World/Net/NetReplication.h"
#include "World/Net/ScriptRepState.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "Scripting/Lua/ScriptTypeRegistry.h"
#include "Scripting/Lua/Stack.h"
#include "Scripting/Lua/VariadicArgs.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Subsystems/WorldSettings.h"
#include "UI/RmlUiBridge.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/entity/systems/EntitySystem.h"
#include "WorldLuaSubsystem.h"

namespace Lumina
{
    
    
    namespace
    {
        bool NetIsServerMode(ENetMode Mode)
        {
            return Mode == ENetMode::ListenServer || Mode == ENetMode::DedicatedServer;
        }
    }

    bool FNetLuaInterface::IsServer() const
    {
        return World != nullptr && World->IsNetServer();
    }
    
    bool FNetLuaInterface::IsClient() const
    {
        return World != nullptr && World->GetNetMode() == ENetMode::Client;
    }
    
    bool FNetLuaInterface::IsStandalone() const
    {
        return World == nullptr || World->GetNetMode() == ENetMode::Standalone;
    }
    
    bool FNetLuaInterface::IsNetworked() const
    {
        return !IsStandalone();
    }

    int32 FNetLuaInterface::GetConnectedClients() const
    {
        return World != nullptr ? World->GetConnectedClientCount() : 0;
    }

    bool FNetLuaInterface::IsConnected() const
    {
        if (World == nullptr)
        {
            return false;
        }
        
        if (World->IsNetServer())
        {
            return World->GetConnectedClientCount() > 0;
        }
        const FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        return State != nullptr && State->bClientConnected;
    }

    uint32 FNetLuaInterface::GetUniqueId() const
    {
        if (World == nullptr)
        {
            return ServerPeerId;
        }
        
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        return State != nullptr ? State->LocalPeerId : ServerPeerId;
    }

    int32 FNetLuaInterface::GetConnectionCount() const
    {
        if (World == nullptr)
        {
            return 0;
        }
        
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        return State != nullptr ? (int32)State->ConnectedClientIds.size() : 0;
    }

    uint32 FNetLuaInterface::GetConnectionAt(int32 Index) const
    {
        if (World == nullptr || Index < 0)
        {
            return 0;
        }
        
        FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
        if (State == nullptr || Index >= (int32)State->ConnectedClientIds.size()) { return 0; }
        return State->ConnectedClientIds[(size_t)Index];
    }

    bool FNetLuaInterface::HasAuthority(entt::entity Entity) const
    {
        if (World == nullptr)
        {
            return false;
        }
        
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net == nullptr ? IsStandalone() || IsServer() : Net->LocalRole == ENetRole::Authority;
    }

    bool FNetLuaInterface::IsLocallyOwned(entt::entity Entity) const
    {
        if (World == nullptr)
        {
            return false;
        }
        
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr && Net->LocalRole == ENetRole::AutonomousProxy;
    }

    int32 FNetLuaInterface::GetLocalRole(entt::entity Entity) const
    {
        if (World == nullptr)
        {
            return (int32)ENetRole::None;
        }
        
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr ? (int32)Net->LocalRole : (int32)ENetRole::None;
    }

    int32 FNetLuaInterface::GetRemoteRole(entt::entity Entity) const
    {
        if (World == nullptr)
        {
            return (int32)ENetRole::None;
        }
        
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr ? (int32)Net->RemoteRole : (int32)ENetRole::None;
    }

    uint32 FNetLuaInterface::GetOwner(entt::entity Entity) const
    {
        if (World == nullptr)
        {
            return 0;
        }
        
        const SNetworkComponent* Net = World->GetEntityRegistry().try_get<SNetworkComponent>(Entity);
        return Net != nullptr ? Net->OwningConnectionId : 0u;
    }

    void FNetLuaInterface::SetOwner(entt::entity Entity, uint32 ConnectionId)
    {
        if (World == nullptr || !IsServer()) { return; } // only the authority assigns ownership
        FEntityRegistry& Registry = World->GetEntityRegistry();
        SNetworkComponent* Net = Registry.try_get<SNetworkComponent>(Entity);
        if (Net == nullptr) { return; }
        Net->OwningConnectionId = ConnectionId;
        if (FNetWorldState* State = Registry.ctx().find<FNetWorldState>())
        {
            State->bOwnershipDirty = true; // replicate the new owner to clients next tick
        }
    }

    void FNetLuaInterface::MarkDirty(entt::entity Entity)
    {
        if (World == nullptr || !IsServer()) { return; } // only the authority replicates
        FEntityRegistry& Registry = World->GetEntityRegistry();
        if (Registry.valid(Entity) && Registry.all_of<SNetworkComponent>(Entity))
        {
            Registry.emplace_or_replace<FNetDirty>(Entity);
        }
    }

    entt::entity FNetLuaInterface::GetLocallyOwnedEntity() const
    {
        if (World == nullptr) { return entt::null; }
        FEntityRegistry& Registry = World->GetEntityRegistry();
        for (entt::entity Entity : Registry.view<SNetworkComponent>())
        {
            if (Registry.get<SNetworkComponent>(Entity).LocalRole == ENetRole::AutonomousProxy)
            {
                return Entity;
            }
        }
        return entt::null;
    }

    void FNetLuaInterface::Host(FStringView Map, int32 Port) const
    {
        if (GEngine != nullptr)
        {
            GEngine->HostLevel(Map, Port > 0 ? static_cast<uint16>(Port) : 7777);
        }
    }

    void FNetLuaInterface::Connect(FStringView InHost, int32 Port) const
    {
        if (GEngine != nullptr)
        {
            GEngine->ConnectToServer(InHost, Port > 0 ? static_cast<uint16>(Port) : 7777);
        }
    }

    //~ World.Debug.* -- screen-space debug text + world debug shapes, forwarded to this world's draw
    // interface. Trailing args are optional; Dev/Debug only (the draws are no-ops in Shipping).
    void FWorldDebugInterface::DrawText(FStringView Text, TOptional<FVector4> Color)
    {
        if (World)
        {
            World->DrawDebugText(FString(Text), Color.value_or(FVector4(1.0f)));
        }
    }
    void FWorldDebugInterface::DrawLine(FVector3 Start, FVector3 End, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
    {
        if (World)
        {
            World->DrawLine(Start, End, Color, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
    }
    void FWorldDebugInterface::DrawBox(FVector3 Center, FVector3 HalfExtents, FQuat Rotation, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
    {
        if (World)
        {
            World->DrawBox(Center, HalfExtents, Rotation, Color, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
    }
    void FWorldDebugInterface::DrawSphere(FVector3 Center, float Radius, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
    {
        if (World)
        {
            World->DrawSphere(Center, Radius, Color, 16, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
    }
    void FWorldDebugInterface::DrawCapsule(FVector3 Start, FVector3 End, float Radius, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
    {
        if (World)
        {
            World->DrawCapsule(Start, End, Radius, Color, 16, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
    }
    void FWorldDebugInterface::DrawCone(FVector3 Apex, FVector3 Direction, float AngleRadians, float Length, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
    {
        if (World)
        {
            World->DrawCone(Apex, Direction, AngleRadians, Length, Color, 16, 4, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
    }
    void FWorldDebugInterface::DrawArrow(FVector3 Start, FVector3 Direction, float Length, FVector4 Color, TOptional<float> Thickness, TOptional<bool> bDepthTest, TOptional<float> Duration)
    {
        if (World)
        {
            World->DrawArrow(Start, Direction, Length, Color, Thickness.value_or(1.0f), bDepthTest.value_or(true), Duration.value_or(0.0f));
        }
    }

    CWorld::CWorld()
        : SingletonEntity(entt::null)
        , SystemContext(this)
        , LineBatcherComponent(nullptr)
        , TriangleBatcherComponent(nullptr)
    {
        NetInterface.World = this;
        DebugInterface.World = this;
    }

    void CWorld::PaintRenderTarget(CTextureRenderTarget* Target, const FVector2& UV, float RadiusUV, const FVector4& Color, float Strength, float Hardness, CTexture* BrushMask)
    {
        if (Target == nullptr || Target->GetRHIRef() == nullptr)
        {
            return;
        }

        FTexturePaintOp Op;
        Op.Target     = Target->GetTextureResource().RHIImage;
        Op.Mode       = FTexturePaintOp::EMode::Paint;
        Op.CenterUV   = UV;
        Op.RadiusUV   = RadiusUV;
        Op.Color      = Color;
        Op.Strength   = Strength;
        Op.Hardness   = Hardness;
        Op.BrushIndex = (BrushMask != nullptr && BrushMask->GetRHIRef() != nullptr) ? BrushMask->GetRHIRef()->GetResourceID() : -1;
        RenderTargetPaintQueue.enqueue(Move(Op));
    }

    void CWorld::ClearRenderTarget(CTextureRenderTarget* Target, const FVector4& Color)
    {
        if (Target == nullptr || Target->GetRHIRef() == nullptr)
        {
            return;
        }

        FTexturePaintOp Op;
        Op.Target = Target->GetTextureResource().RHIImage;
        Op.Mode   = FTexturePaintOp::EMode::Clear;
        Op.Color  = Color;
        RenderTargetPaintQueue.enqueue(Move(Op));
    }

    void CWorld::DrainRenderTargetPaints(TVector<FTexturePaintOp>& OutOps)
    {
        FTexturePaintOp Op;
        while (RenderTargetPaintQueue.try_dequeue(Op))
        {
            OutOps.push_back(Move(Op));
        }
    }

    void CWorld::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);

        if (Ar.IsReading())
        {
            RegistryPending.clear<>();
            ECS::Utils::SerializeRegistry(Ar, RegistryPending);
        }
        else
        {
            // A freshly-loaded asset keeps entities in RegistryPending until InitializeWorld swaps them
            // into EntityRegistry; DuplicateWorld serializes pre-init. Write from whichever holds the data.
            FEntityRegistry& Source = (!EntityRegistry.storage<entt::entity>().empty())
                ? EntityRegistry
                : RegistryPending;
            ECS::Utils::SerializeRegistry(Ar, Source);
        }
    }

    void CWorld::PreLoad()
    {
    }

    void CWorld::PostLoad()
    {
    }
    
    void CWorld::InitializeWorld(EWorldType InWorldType)
    {
        using namespace entt::literals;
        
        WorldType = InWorldType;

        // Skip spawning instances whose source prefab was deleted while this world was unloaded: their
        // SourcePrefab deserialized to null. Cull the pending set so they never enter the live registry
        // (no transforms, physics bodies, scripts, or render data are ever created for them).
        CPrefab::CullOrphanedInstances(RegistryPending);

        EntityRegistry.swap(RegistryPending);
        RegistryPending = {};

        CPrefab::RefreshAllInstancesInWorld(this);
        
        EntityRegistry.compact();
        
        if (GetNetMode() == ENetMode::Client)
        {
            TVector<entt::entity> ServerOnly;
            for (entt::entity Entity : EntityRegistry.view<SNetworkComponent>())
            {
                if (!EntityRegistry.get<SNetworkComponent>(Entity).bNetLoadOnClient)
                {
                    ServerOnly.push_back(Entity);
                }
            }
            for (entt::entity Entity : ServerOnly)
            {
                EntityRegistry.destroy(Entity);
            }
        }

        EntityRegistry.ctx().emplace<entt::dispatcher&>(SingletonDispatcher);
        
        auto WorldSettingsView = EntityRegistry.view<SDefaultWorldSettings>();
        for (auto Entity : WorldSettingsView)
        {
            if (!ALERT_IF_NOT(WorldSettingsView->size() == 1, "Multiple world settings were detected in the world! {}", WorldSettingsView->size()))
            {
                EntityRegistry.clear<SDefaultWorldSettings>();
                break;
            }
            
            SingletonEntity = Entity;
            break;
        }
        
        if (!EntityRegistry.valid(SingletonEntity))
        {
            SingletonEntity = EntityRegistry.create();
            EntityRegistry.emplace<SDefaultWorldSettings>(SingletonEntity);
        }
        
        LineBatcherComponent = &EntityRegistry.emplace<FLineBatcherComponent>(SingletonEntity);
        TriangleBatcherComponent = &EntityRegistry.emplace<FTriangleBatcherComponent>(SingletonEntity);
        EntityRegistry.emplace<FSingletonEntityTag>(SingletonEntity);
        EntityRegistry.emplace<FHideInSceneOutliner>(SingletonEntity);
        
        // Physics scene only for simulating worlds; Jolt reserves ~hundreds of MB up front.
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene = Physics::GetPhysicsContext()->CreatePhysicsScene(this);
        }
        // Emplaced even when null so ctx().get<>() consumers find the key (value is null in
        // non-simulating worlds and must be null-checked).
        EntityRegistry.ctx().emplace<Physics::IPhysicsScene*>(PhysicsScene.get());
        EntityRegistry.ctx().emplace<FSystemContext&>(SystemContext);
        EntityRegistry.ctx().emplace<CWorld*>(this);

        // Per-world subsystem singletons ticked by their systems: SEventBusSystem flushes FLuaEventBus,
        // STimerSystem advances FTimerManager (both at FrameStart). Scripts reach them by ctx address.
        EntityRegistry.ctx().emplace<FLuaEventBus>();
        EntityRegistry.ctx().emplace<FTimerManager>();

        // System-produced singletons: SCameraSystem owns FCameraGlobalState (active camera + blend) and
        // writes FResolvedSceneView (read in Extract); SScriptSystem owns FScriptFixedUpdateState.
        EntityRegistry.ctx().emplace<FCameraGlobalState>();
        EntityRegistry.ctx().emplace<FResolvedSceneView>();
        EntityRegistry.ctx().emplace<FScriptFixedUpdateState>();

        CreateRenderer();
        UIContext = RmlUi::CreateWorldUI(this);

        // Seed the per-world disabled-system set from the saved world settings before registering systems,
        // so disabled systems are never constructed/started. Tolerant of stale names (ignored below).
        DisabledSystems.clear();
        for (const FName& Name : GetDefaultWorldSettings().DisabledSystems)
        {
            DisabledSystems.insert(Name);
        }
        PendingDisabledSystems = DisabledSystems;

        // Seed the assigned script-system paths from saved settings before registering, so they load and
        // start alongside native systems below.
        ScriptSystems.clear();
        for (const FString& Path : GetDefaultWorldSettings().ScriptSystems)
        {
            ScriptSystems.push_back(Path);
        }
        PendingScriptSystems = ScriptSystems;

        RegisterSystems();
        
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene->Simulate();
        }
        
        ForEachUniqueSystem([&](const FActiveSystem& System)
        {
            if (System.Startup)
            {
                System.Startup(System.Self, SystemContext);
            }
        });

        EntityRegistry.on_destroy   <FRelationshipComponent>()      .connect<&ThisClass::OnRelationshipComponentDestroyed>(this);
        EntityRegistry.on_construct <STransformComponent>()         .connect<&ThisClass::OnTransformComponentConstruct>(this);
        EntityRegistry.on_construct <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentConstruct>(this);
        EntityRegistry.on_update    <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentUpdated>(this);
        EntityRegistry.on_destroy   <SScriptComponent>()            .connect<&ThisClass::OnScriptComponentDestroyed>(this);
        EntityRegistry.on_destroy   <SWidgetComponent>()            .connect<&ThisClass::OnWidgetComponentDestroyed>(this);
        EntityRegistry.on_construct <SInputComponent>()             .connect<&ThisClass::OnInputComponentConstruct>(this);
        EntityRegistry.on_destroy   <SInputComponent>()             .connect<&ThisClass::OnInputComponentDestroyed>(this);
        SystemContext.EventSink     <FSwitchActiveCameraEvent>()    .connect<&ThisClass::OnChangeCameraEvent>(this);
        SystemContext.EventSink     <FScriptComponentPendingReady>().connect<&ThisClass::OnScriptComponentPendingReady>(this);

        ScriptReloadedHandle = Lua::FScriptingContext::Get().OnScriptLoaded.AddMember(this, &ThisClass::OnScriptSourceReloaded);
        
        if (lua_State* VM = Lua::FScriptingContext::Get().GetVM())
        {
            lua_newtable(VM);
            DrawInterfaceRef = Lua::FRef(VM, -1);
            auto* DI = static_cast<IPrimitiveDrawInterface*>(this);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawLine>   ("DrawLine",    DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawBox>    ("DrawBox",     DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawSphere> ("DrawSphere",  DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawCapsule>("DrawCapsule", DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawCone>   ("DrawCone",    DI);
            DrawInterfaceRef.SetFunction<&IPrimitiveDrawInterface::DrawArrow>  ("DrawArrow",   DI);
        }
        
        auto TransformView = EntityRegistry.view<STransformComponent>();
        TransformView.each([&](entt::entity Entity, STransformComponent& TransformComponent)
        {
            TransformComponent.Registry = &EntityRegistry;
            TransformComponent.Entity = Entity;
        });

        // Bind loaded input components to this world so their queries resolve to this world's viewport
        // (hooks connect after the load swap, so pre-existing components miss on_construct).
        auto InputView = EntityRegistry.view<SInputComponent>();
        InputView.each([&](SInputComponent& InputComponent)
        {
            InputComponent.World = this;
        });
        
        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            const auto AnyCameraView = EntityRegistry.view<SCameraComponent>();
            if (AnyCameraView.begin() == AnyCameraView.end())
            {
                LOG_WARN("CWorld::Initialize: world '{}' has no camera entity; spawning a fallback at (0, 2, 5) looking at origin. Add a camera entity for proper gameplay.",
                    GetName());

                constexpr FVector3 FallbackPos(0.0f, 2.0f, 5.0f);

                const entt::entity Fallback = EntityRegistry.create();
                STransformComponent& Xf = EntityRegistry.emplace<STransformComponent>(Fallback);
                Xf.LocalTransform.Location = FallbackPos;
                Xf.LocalTransform.Rotation = Math::FindLookAtRotation(FVector3(0.0f), FallbackPos);

                SCameraComponent& Cam = EntityRegistry.emplace<SCameraComponent>(Fallback);
                Cam.bAutoActivate = true;
            }
        }

        auto CameraView = EntityRegistry.view<SCameraComponent>(entt::exclude<SDisabledTag>);
        CameraView.each([&](entt::entity Entity, const SCameraComponent& Camera)
        {
           if (Camera.bAutoActivate)
           {
               SingletonDispatcher.trigger<FSwitchActiveCameraEvent>(FSwitchActiveCameraEvent{Entity});
           }
        });
        
        InitializeScriptEntities();
        
        if (WorldType == EWorldType::Simulation || WorldType == EWorldType::Game)
        {
            bPaused = false;
        }
        
        bInitializing = false;
    }
    
    void CWorld::TeardownWorld()
    {
        // No render thread / RHI / audio device in a headless process.
        if (!GIsHeadless)
        {
            FlushRenderingCommands();
            GRenderContext->WaitIdle();
        }

        RmlUi::DestroyWorldUI(this);
        UIContext.reset();

        if (GAudioContext != nullptr)
        {
            GAudioContext->StopAllSounds();
        }

        if (ScriptReloadedHandle.IsValid())
        {
            Lua::FScriptingContext::Get().OnScriptLoaded.Remove(ScriptReloadedHandle);
            ScriptReloadedHandle = {};
        }

        EntityRegistry.on_destroy<FRelationshipComponent>().disconnect<&ThisClass::OnRelationshipComponentDestroyed>(this);
        EntityRegistry.on_update<SScriptComponent>().disconnect<&ThisClass::OnScriptComponentUpdated>(this);
        EntityRegistry.on_construct<SScriptComponent>().disconnect<&ThisClass::OnScriptComponentConstruct>(this);
        
        ForEachUniqueSystem([&](const FActiveSystem& System)
        {
            if (System.Teardown)
            {
                System.Teardown(System.Self, SystemContext);
            }
        });

        // Release the loaded script systems (drops their Lua refs) after their Teardown has run.
        ScriptSystemInstances.clear();
        ScriptSystemsToReload.clear();

        if (WorldType == EWorldType::Game || WorldType == EWorldType::Simulation)
        {
            PhysicsScene->StopSimulate();
        }
        
        EntityRegistry.ctx().get<FLuaEventBus>().Clear();
        EntityRegistry.ctx().get<FTimerManager>().Clear();

        RegistryPending.clear<>();
        EntityRegistry.clear<>();
        PhysicsScene.reset();
        DestroyRenderer();
        
        FCoreDelegates::PostWorldUnload.Broadcast();

        Lua::FScriptingContext::Get().RunGC();
    }
    
    static const char* StageName(EUpdateStage Stage)
    {
        switch (Stage)
        {
        case EUpdateStage::FrameStart:    return "FrameStart";
        case EUpdateStage::PrePhysics:    return "PrePhysics";
        case EUpdateStage::DuringPhysics: return "DuringPhysics";
        case EUpdateStage::PostPhysics:   return "PostPhysics";
        case EUpdateStage::FrameEnd:      return "FrameEnd";
        case EUpdateStage::Paused:        return "Paused";
        default:                          return "Unknown";
        }
    }

    void CWorld::Update(const FUpdateContext& Context)
    {
        LUMINA_PROFILE_SCOPE();

        const EUpdateStage Stage = Context.GetUpdateStage();

        FCPUProfiler::Get().PushWorldTarget(this);
        struct FPopGuard { ~FPopGuard() { FCPUProfiler::Get().PopTarget(); } } PopGuard;

        CPU_PROFILE_SCOPE(StageName(Stage));

        // Reconcile any deferred system enable/disable before the gate/tick, so a toggle requested mid-frame
        // is applied here (between frames) and never inside a running system batch.
        ApplyPendingSystemChanges();

        if (Stage == EUpdateStage::FrameStart)
        {
            DeltaTime = Context.GetDeltaTime() * GetDefaultWorldSettings().DeltaTimeScale;
            TimeSinceCreation += DeltaTime;

#if USING(WITH_EDITOR)
            // Migrate runtime-component storage whose schema was edited. Editor-only (schemas immutable
            // at runtime); runs even while paused so editor worlds reflect live edits. Cheap revision compare.
            ECS::Utils::RefreshRuntimeComponentSchemas(EntityRegistry);
#endif
        }

        if (bPaused && Stage != EUpdateStage::Paused || (!bPaused && Stage == EUpdateStage::Paused))
        {
            return;
        }

        SystemContext.DeltaTime     = DeltaTime;
        SystemContext.Time          = TimeSinceCreation;
        SystemContext.UpdateStage   = Stage;

        {
            CPU_PROFILE_SCOPE("PendingReady Dispatch");
            SingletonDispatcher.update<FScriptComponentPendingReady>();
        }

        // Deferred Lua events + timers run inside TickSystems now (SEventBusSystem / STimerSystem,
        // both FrameStart/Highest), so they tick before gameplay systems just as the old inline block did.
        {
            CPU_PROFILE_SCOPE("Systems");
            TickSystems(SystemContext);
        }
    }

    bool CWorld::IsScriptActiveInWorld(entt::entity Entity) const
    {
        if (WorldType == EWorldType::Editor)
        {
            return EntityRegistry.all_of<FScriptHasEditorUpdateFn>(Entity);
        }
        return true;
    }

    void CWorld::TickPhysics()
    {
        LUMINA_PROFILE_SCOPE();

        if (bPaused || PhysicsScene == nullptr)
        {
            return;
        }

        CPU_PROFILE_SCOPE_COLOR("Physics", FColor(0.20f, 0.75f, 0.90f));
        PhysicsScene->Update(DeltaTime);
    }

    void CWorld::DispatchPhysicsEvents()
    {
        if (PhysicsScene == nullptr)
        {
            return;
        }

        PhysicsScene->DispatchPendingEvents();
    }

    void CWorld::Render(ICommandList& CmdList, uint8 FrameIndex) const
    {
        LUMINA_PROFILE_SCOPE();

        RmlUi::RenderWorldWidgets(this, CmdList);

        if (RenderScene)
        {
            RenderScene->RenderView_RenderThread(CmdList, FrameIndex);
        }

        // Composite this world's UI onto its render target, right after its scene.
        RmlUi::RenderWorldUI(this, CmdList);
    }

    void CWorld::Extract()
    {
        LUMINA_PROFILE_SCOPE();

        RmlUi::TickWorldUI(this);
        RmlUi::TickWorldWidgets(this);

        // Renderer-less worlds (headless process or dedicated server) have nothing to extract. The
        // RmlUi ticks above are safe no-ops when uninitialized; bail before touching RenderScene.
        if (RenderScene == nullptr)
        {
            return;
        }

        // SCameraSystem resolves the active view + post-process volumes into this
        // singleton at the tail of the update; we just forward it to the renderer.
        const FResolvedSceneView& View = EntityRegistry.ctx().get<FResolvedSceneView>();

        if (View.bHasView)
        {
            RenderScene->SetActivePostProcessMaterials(View.PostProcessMaterials);
            RenderScene->Extract(View.ViewVolume, View.bHasPostProcess ? &View.PostProcess : nullptr);
            return;
        }

        RenderScene->SetActivePostProcessMaterials({});
        RenderScene->Extract(FViewVolume{}, nullptr);
    }

    void CWorld::OnScriptComponentPendingReady(const FScriptComponentPendingReady& Event)
    {
        entt::entity Entity = Event.Entity;
        
        if (!EntityRegistry.valid(Entity))
        {
            return;
        }
            
        SScriptComponent* ScriptComponent = EntityRegistry.try_get<SScriptComponent>(Entity);
        if (!ScriptComponent)
        {
            return;
        }
            
        if (ScriptComponent->ReadyFunc.IsValid())
        {
            ScriptComponent->Script->PublishThreadContext();
            // Coroutine so OnReady can yield (wait/await) and resume later.
            ScriptComponent->Script->InvokeAsCoroutine(ScriptComponent->ReadyFunc, ScriptComponent->Script->Reference);
        }
    }

    void CWorld::InitializeScriptEntities()
    {
        auto ScriptView = EntityRegistry.view<SScriptComponent>(entt::exclude<SDisabledTag>);

        ScriptView.each([&](entt::entity Entity, SScriptComponent& Component)
        {
            SetupScriptComponent(Entity, Component);
        });

        auto Visit = [&](entt::entity Entity, const Lua::FRef SScriptComponent::*Hook)
        {
            if (EntityRegistry.any_of<SDisabledTag>(Entity))
            {
                return;
            }
            SScriptComponent* Component = EntityRegistry.try_get<SScriptComponent>(Entity);
            if (!Component || Component->Script == nullptr)
            {
                return;
            }
            if (!IsScriptActiveInWorld(Entity))
            {
                return;
            }
            const Lua::FRef& HookRef = Component->*Hook;
            if (HookRef.IsValid())
            {
                Component->Script->PublishThreadContext();
                // OnAttach/OnReady run on a coroutine so they can yield (wait/await) and resume later.
                Component->Script->InvokeAsCoroutine(HookRef, Component->Script->Reference);
            }
        };

        auto InvokeAttach = [&](entt::entity Entity) { Visit(Entity, &SScriptComponent::AttachFunc); };
        auto InvokeReady  = [&](entt::entity Entity) { Visit(Entity, &SScriptComponent::ReadyFunc);  };

        auto RelationshipRoots = EntityRegistry.view<FRelationshipComponent>(entt::exclude<SDisabledTag>);

        RelationshipRoots.each([&](entt::entity Entity, const FRelationshipComponent& Relationship)
        {
            if (Relationship.Parent != entt::null)
            {
                return;
            }
            InvokeAttach(Entity);
            ECS::Utils::ForEachDescendant(EntityRegistry, Entity, InvokeAttach);
        });
        
        // Scriptless-root entities fall outside the relationship view; handle here.
        ScriptView.each([&](entt::entity Entity, SScriptComponent&)
        {
            if (!EntityRegistry.all_of<FRelationshipComponent>(Entity))
            {
                InvokeAttach(Entity);
            }
        });

        RelationshipRoots.each([&](entt::entity Entity, const FRelationshipComponent& Relationship)
        {
            if (Relationship.Parent != entt::null)
            {
                return;
            }
            ECS::Utils::ForEachDescendantReverse(EntityRegistry, Entity, InvokeReady);
            InvokeReady(Entity);
        });

        ScriptView.each([&](entt::entity Entity, SScriptComponent&)
        {
            if (!EntityRegistry.all_of<FRelationshipComponent>(Entity))
            {
                InvokeReady(Entity);
            }
        });
    }

    entt::entity CWorld::ConstructEntity(const FName& Name, const FTransform& Transform)
    {
        entt::entity NewEntity = GetEntityRegistry().create();
        
        FName ActualName = Name;
        if (ActualName == NAME_None)
        {
            FFixedString StringName;
            StringName.append_convert(Name + eastl::to_string(entt::to_integral(NewEntity)));
            ActualName = StringName;
        }
        
        EntityRegistry.emplace<SNameComponent>(NewEntity).Name = Name;
        EntityRegistry.emplace<STransformComponent>(NewEntity, Transform);
        
        return NewEntity;
    }
    
    bool CWorld::FractureEntity(entt::entity Entity, const FVector3& Origin, float Strength)
    {
        LUMINA_PROFILE_SCOPE();

        if (!EntityRegistry.valid(Entity))
        {
            return false;
        }

        SDestructibleComponent* Destructible = EntityRegistry.try_get<SDestructibleComponent>(Entity);
        if (Destructible == nullptr || Destructible->bFractured)
        {
            return false;
        }

        // Resolve the mesh to shatter: explicit fragment override, else the entity's own static mesh.
        SStaticMeshComponent* MeshComp = EntityRegistry.try_get<SStaticMeshComponent>(Entity);
        CStaticMesh* SourceMesh = Destructible->FragmentMesh.Get();
        if (SourceMesh == nullptr && MeshComp != nullptr)
        {
            SourceMesh = MeshComp->StaticMesh.Get();
        }

        if (SourceMesh == nullptr)
        {
            LOG_WARN("FractureEntity: entity {} has no mesh to fracture", entt::to_integral(Entity));
            return false;
        }

        FTransform OwnerTransform = EntityRegistry.get<STransformComponent>(Entity).GetWorldTransform();
        
        FVector3 InheritedVelocity(0.0f);
        if (PhysicsScene)
        {
            if (const SRigidBodyComponent* RB = EntityRegistry.try_get<SRigidBodyComponent>(Entity))
            {
                if (RB->BodyID != 0xFFFFFFFFu)
                {
                    OwnerTransform.Location = PhysicsScene->GetBodyPosition(RB->BodyID);
                    OwnerTransform.Rotation = PhysicsScene->GetBodyRotation(RB->BodyID);
                    InheritedVelocity       = PhysicsScene->GetLinearVelocity(RB->BodyID);
                }
            }
        }

        const FVector3 OwnerScale = OwnerTransform.Scale;

        const float LaunchSpeed = Strength > 0.0f ? Strength : Destructible->ExplosionStrength;
        const float SpinSpeed   = Destructible->SpinStrength;

        // Deterministic per-fragment jitter (good for replays / lockstep): hash the index.
        auto Hash01 = [](uint32 V) -> float
        {
            V ^= V >> 16; V *= 0x7feb352dU; V ^= V >> 15; V *= 0x846ca68bU; V ^= V >> 16;
            return static_cast<float>(V) / static_cast<float>(0xFFFFFFFFU);
        };

        // Inherited momentum + an outward blast (radial from Origin) + random spin on a fresh body.
        auto LaunchBody = [&](uint32 BodyID, const FVector3& WorldCenter, uint32 Seed)
        {
            if (!PhysicsScene || BodyID == 0xFFFFFFFFu)
            {
                return;
            }
            FVector3 Direction = WorldCenter - Origin;
            const float Distance = Math::Length(Direction);
            Direction = Distance > 1e-4f
                ? Direction / Distance
                : Math::Normalize(FVector3(Hash01(Seed) - 0.5f, Hash01(Seed + 1) + 0.25f, Hash01(Seed + 2) - 0.5f));

            const float SpeedJitter = 0.7f + 0.6f * Hash01(Seed + 3);
            const FVector3 LaunchVelocity = InheritedVelocity
                + Direction * (LaunchSpeed * SpeedJitter)
                + FVector3(0.0f, LaunchSpeed * 0.2f, 0.0f);
            PhysicsScene->OnSetVelocityEvent(SSetVelocityEvent{ BodyID, LaunchVelocity });

            if (SpinSpeed > 0.0f)
            {
                const FVector3 Spin(Hash01(Seed + 4) - 0.5f, Hash01(Seed + 5) - 0.5f, Hash01(Seed + 6) - 0.5f);
                PhysicsScene->OnSetAngularVelocityEvent(SSetAngularVelocityEvent{ BodyID, Spin * (2.0f * SpinSpeed) });
            }
        };

        int32 Spawned = 0;

        // Source of pieces: an assigned collection if present, else a convex Voronoi fracture
        // generated on the fly from the mesh bounds (real chunks with zero authoring).
        const FFractureData* CollectionData = nullptr;
        if (CGeometryCollection* Collection = Destructible->Collection.Get())
        {
            if (Collection->GetNumPieces() > 0)
            {
                CollectionData = &Collection->GetFractureData();
            }
        }

        TVector<FFracturePiece> GeneratedPieces;
        if (CollectionData == nullptr)
        {
            FFractureSettings Settings;
            Settings.NumPieces = Destructible->FragmentCount;
            Settings.Seed      = entt::to_integral(Entity) * 2654435761U + 1u;
            Fracture::GenerateConvexFracture(SourceMesh, Settings, GeneratedPieces);
        }

        const TVector<FFracturePiece>& Pieces = CollectionData ? CollectionData->Pieces : GeneratedPieces;

        // Create all fragment bodies in one batch (AddBodiesPrepare/Finalize). BodyIDs valid only after
        // EndBodyBatch, so collect launch impulses and apply them once inserted.
        struct FPendingLaunch { entt::entity Fragment; FVector3 Center; uint32 Seed; };
        TVector<FPendingLaunch> PendingLaunches;
        PendingLaunches.reserve(Pieces.size());

        // Cap fragments at physics body headroom; overflowing Jolt's body/contact buffers trips a hard
        // assert, so clamp + warn instead. Raise World Settings > Physics > Max* for denser destruction.
        uint32 MaxFragments = 0xFFFFFFFFu;
        if (PhysicsScene)
        {
            const uint32 MaxBodies = PhysicsScene->GetMaxBodyCount();
            const uint32 Used      = Math::Min(PhysicsScene->GetBodyCount(), MaxBodies);
            const uint32 Headroom  = MaxBodies - Used;
            MaxFragments = Headroom > 16 ? Headroom - 16 : 0;

            const uint32 Desired = Pieces.empty()
                ? (uint32)Math::Clamp(Destructible->FragmentCount, 2, 512)
                : (uint32)Pieces.size();
            if (Desired > MaxFragments)
            {
                LOG_WARN("FractureEntity: clamped {} fragments to {} (physics body headroom {}/{}). Raise World Settings > Physics > MaxPhysicsBodies.",
                    Desired, MaxFragments, Used, MaxBodies);
            }
        }

        if (PhysicsScene)
        {
            PhysicsScene->BeginBodyBatch();
        }

        if (!Pieces.empty())
        {
            const TVector<TObjectPtr<CMaterialInterface>>& PieceMaterials =
                (CollectionData && !Destructible->Collection->Materials.empty())
                    ? Destructible->Collection->Materials
                    : SourceMesh->Materials;

            // Pre-baked collections cache piece meshes (built at load), so fracture does no per-piece
            // meshlet build / upload. The on-the-fly Voronoi path has no cache and builds each inline.
            const TVector<TObjectPtr<CStaticMesh>>* CachedMeshes =
                CollectionData ? &Destructible->Collection->GetPieceMeshes() : nullptr;

            for (size_t PieceIndex = 0; PieceIndex < Pieces.size() && (uint32)Spawned < MaxFragments; ++PieceIndex)
            {
                const FFracturePiece& Piece = Pieces[PieceIndex];

                CStaticMesh* PieceMesh = CachedMeshes
                    ? (PieceIndex < CachedMeshes->size() ? (*CachedMeshes)[PieceIndex].Get() : nullptr)
                    : Fracture::BuildPieceMesh(Piece, PieceMaterials, "GCPiece");
                if (PieceMesh == nullptr)
                {
                    continue;
                }

                // BuildPieceMesh recenters geometry to the piece centroid (natural pivot + CoM); place
                // the entity at the centroid's world position so pieces reconstruct the object at t=0.
                const FVector3 WorldCenter = OwnerTransform.Location + OwnerTransform.Rotation * (OwnerTransform.Scale * Piece.Center);
                FTransform PieceTransform;
                PieceTransform.Location = WorldCenter;
                PieceTransform.Rotation = OwnerTransform.Rotation;
                PieceTransform.Scale    = OwnerTransform.Scale;

                const entt::entity Fragment = ConstructEntity("Fragment", PieceTransform);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Fragment);

                EntityRegistry.emplace<SStaticMeshComponent>(Fragment).StaticMesh = PieceMesh;

                // The collider's on_construct builds the Jolt shape synchronously, so Mesh + bConvex must
                // be set before insertion -- otherwise the body uses default (non-convex) settings, forced Static.
                SMeshColliderComponent ColliderDesc;
                ColliderDesc.Mesh    = PieceMesh;
                ColliderDesc.bConvex = true;
                EntityRegistry.emplace<SMeshColliderComponent>(Fragment, std::move(ColliderDesc));

                EntityRegistry.emplace<SLifetimeComponent>(Fragment).Lifetime = Destructible->FragmentLifetime;
                EntityRegistry.emplace<SFragmentComponent>(Fragment).Source   = entt::to_integral(Entity);
                
                PendingLaunches.push_back({ Fragment, WorldCenter, entt::to_integral(Fragment) + static_cast<uint32>(Spawned) });

                ++Spawned;
            }
        }
        else
        {
            // Fallback (degenerate fracture): subdivide the bounds into a grid of textured box chunks.
            const FAABB& LocalBounds = SourceMesh->GetAABB();
            const FVector3 LocalExtent = Math::Max(LocalBounds.GetSize(), FVector3(0.01f));
            const FVector3 LocalCenter = LocalBounds.GetCenter();
            const int32 Target = Math::Clamp(Destructible->FragmentCount, 2, 512);
            const int32 Dims   = Math::Max(1, static_cast<int32>(std::ceil(std::cbrt(static_cast<float>(Target)))));
            const FVector3 LocalCell = LocalExtent / static_cast<float>(Dims);
            const FVector3 FragScale = OwnerScale / static_cast<float>(Dims);
            const FVector3 ColliderHalf = LocalExtent * 0.5f;
            CStaticMesh* GridMesh = Destructible->FragmentMesh.Get() ? Destructible->FragmentMesh.Get() : SourceMesh;

            for (int32 zi = 0; zi < Dims && Spawned < Target && (uint32)Spawned < MaxFragments; ++zi)
            for (int32 yi = 0; yi < Dims && Spawned < Target && (uint32)Spawned < MaxFragments; ++yi)
            for (int32 xi = 0; xi < Dims && Spawned < Target && (uint32)Spawned < MaxFragments; ++xi)
            {
                const FVector3 CellLocalCenter = LocalBounds.Min + (FVector3(xi, yi, zi) + 0.5f) * LocalCell;
                const FVector3 CellWorldCenter = OwnerTransform.Location + OwnerTransform.Rotation * (OwnerScale * CellLocalCenter);

                FTransform FragmentTransform;
                FragmentTransform.Location = CellWorldCenter - OwnerTransform.Rotation * (FragScale * LocalCenter);
                FragmentTransform.Rotation = OwnerTransform.Rotation;
                FragmentTransform.Scale    = FragScale;

                const entt::entity Fragment = ConstructEntity("Fragment", FragmentTransform);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Fragment);

                SStaticMeshComponent& FragmentMeshComp = EntityRegistry.emplace<SStaticMeshComponent>(Fragment);
                FragmentMeshComp.StaticMesh = GridMesh;
                if (MeshComp != nullptr)
                {
                    FragmentMeshComp.MaterialOverrides = MeshComp->MaterialOverrides;
                }

                // Box collider auto-emplaces a Dynamic rigid body, built synchronously from these
                // settings the instant the component is inserted, so set HalfExtent up front.
                SBoxColliderComponent BoxDesc;
                BoxDesc.HalfExtent = ColliderHalf;
                EntityRegistry.emplace<SBoxColliderComponent>(Fragment, std::move(BoxDesc));

                EntityRegistry.emplace<SLifetimeComponent>(Fragment).Lifetime = Destructible->FragmentLifetime;
                EntityRegistry.emplace<SFragmentComponent>(Fragment).Source   = entt::to_integral(Entity);

                PendingLaunches.push_back({ Fragment, CellWorldCenter, entt::to_integral(Fragment) + static_cast<uint32>(Spawned) });

                ++Spawned;
            }
        }

        // Insert all the queued bodies at once, then apply the launch impulses now that BodyIDs exist.
        if (PhysicsScene)
        {
            PhysicsScene->EndBodyBatch();
        }
        for (const FPendingLaunch& Launch : PendingLaunches)
        {
            LaunchBody(EntityRegistry.get<SRigidBodyComponent>(Launch.Fragment).BodyID, Launch.Center, Launch.Seed);
        }

        Destructible->bFractured = true;

        // Retire the original: strip render + physics now so it vanishes this frame; the lifetime system
        // reaps it at FrameEnd -- safe even when called from the entity's own script callback.
        if (Destructible->bDestroyOriginal)
        {
            EntityRegistry.remove<SStaticMeshComponent>(Entity);
            EntityRegistry.remove<SRigidBodyComponent>(Entity);
            EntityRegistry.remove<SBoxColliderComponent>(Entity);
            EntityRegistry.remove<SSphereColliderComponent>(Entity);
            EntityRegistry.remove<SMeshColliderComponent>(Entity);
            EntityRegistry.emplace_or_replace<SLifetimeComponent>(Entity).Lifetime = 0.01f;
        }

        return Spawned > 0;
    }

    entt::entity CWorld::SpawnPrefab(const FName& Path)
    {
        return SpawnPrefabAt(Path, FTransform(), entt::null);
    }

    entt::entity CWorld::SpawnPrefabAt(const FName& Path, const FTransform& SpawnTransform, entt::entity Parent)
    {
        FAssetData* AssetData = FAssetRegistry::Get().GetAssetByPath(FStringView(Path.c_str()));
        if (AssetData == nullptr)
        {
            LOG_WARN("SpawnPrefab: no asset found at path '{}'", Path.c_str());
            return entt::null;
        }

        CPrefab* Prefab = Cast<CPrefab>(LoadObject<CObject>(AssetData->AssetGUID));
        if (Prefab == nullptr)
        {
            LOG_WARN("SpawnPrefab: asset '{}' is not a CPrefab", Path.c_str());
            return entt::null;
        }

        return Prefab->Instantiate(this, SpawnTransform, Parent);
    }

    void CWorld::SpawnPrefabAsync(const FName& Path, const TFunction<void(entt::entity)>& Callback)
    {
        AsyncLoadObject(Path, [this, Callback, Path](CObject* Object)
        {
            CPrefab* Prefab = Cast<CPrefab>(Object);
            if (Prefab == nullptr)
            {
                LOG_WARN("SpawnPrefab: asset '{}' is not a CPrefab", Path.c_str());
                Callback(entt::null);
                return;
            }

            Callback(Prefab->Instantiate(this, FTransform(), entt::null));
        });
    }

    void CWorld::DuplicateEntity(entt::entity& To, entt::entity From, const TFunctionRef<bool(entt::type_info)>& Callback)
    {
        ASSERT(To != From);

        THashMap<entt::entity, entt::entity> SourceToDuplicate;

        auto DuplicateRecursive = [&](auto& Self, entt::entity Source, entt::entity NewParent) -> entt::entity
        {
            entt::entity NewEntity = EntityRegistry.create();
            SourceToDuplicate[Source] = NewEntity;

            for (auto&& [ID, Storage] : EntityRegistry.storage())
            {
                if (!Callback(Storage.info()))
                {
                    continue;
                }

                // Scripts/rigid bodies can't be bit-copied; re-emplaced below so on_construct fires fresh.
                if (ID == entt::type_hash<FRelationshipComponent>::value()
                    || ID == entt::type_hash<SScriptComponent>::value()
                    || ID == entt::type_hash<SRigidBodyComponent>::value())
                {
                    continue;
                }

                if (Storage.contains(Source) && !Storage.contains(NewEntity))
                {
                    Storage.push(NewEntity, Storage.value(Source));
                }
            }

            // Rebind: bit-copy carries source's self-references (Entity/Registry ptr).
            if (STransformComponent* NewTransform = EntityRegistry.try_get<STransformComponent>(NewEntity))
            {
                NewTransform->Bind(EntityRegistry, NewEntity);
                EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(NewEntity);
            }

            // Remove auto-emplaced default first; emplace_or_replace would fire on_update (no-op), not on_construct.
            if (const SRigidBodyComponent* SourceBody = EntityRegistry.try_get<SRigidBodyComponent>(Source))
            {
                SRigidBodyComponent NewBody = *SourceBody;
                NewBody.BodyID = 0xFFFFFFFF;

                EntityRegistry.remove<SRigidBodyComponent>(NewEntity);
                EntityRegistry.emplace<SRigidBodyComponent>(NewEntity, eastl::move(NewBody));
            }

            // Emplace editable fields only; on_construct loads a unique FScript for the duplicate.
            if (const SScriptComponent* SourceScript = EntityRegistry.try_get<SScriptComponent>(Source))
            {
                SScriptComponent NewScript;
                NewScript.ScriptPath        = SourceScript->ScriptPath;
                NewScript.PropertyOverrides = SourceScript->PropertyOverrides;
                NewScript.UpdateStage       = SourceScript->UpdateStage;
                NewScript.TickRate          = SourceScript->TickRate;
                EntityRegistry.emplace<SScriptComponent>(NewEntity, eastl::move(NewScript));
            }

            if (NewParent != entt::null)
            {
                ECS::Utils::ReparentEntity(EntityRegistry, NewEntity, NewParent);
            }
            else if (FRelationshipComponent* Rel = EntityRegistry.try_get<FRelationshipComponent>(Source))
            {
                if (Rel->Parent != entt::null)
                {
                    ECS::Utils::ReparentEntity(EntityRegistry, NewEntity, Rel->Parent);
                }
            }

            ECS::Utils::ForEachChild(EntityRegistry, Source, [&](entt::entity Child)
            {
                Self(Self, Child, NewEntity);
            });

            return NewEntity;
        };

        To = DuplicateRecursive(DuplicateRecursive, From, entt::null);

        // Fix up entity-handle properties so references between duplicated entities point at the
        // new copies; references to entities outside the duplicated set are left untouched.
        for (auto& [Source, Dup] : SourceToDuplicate)
        {
            ECS::Utils::RemapEntityReferences(EntityRegistry, Dup, SourceToDuplicate, /*bClearUnmapped*/ false);
        }
    }

    void CWorld::DestroyEntity(entt::entity Entity)
    {
        EntityRegistry.destroy(Entity);
    }

    STransformComponent& CWorld::GetEntityTransform(entt::entity Entity)
    {
        return EntityRegistry.get<STransformComponent>(Entity);
    }

    FVector3 CWorld::GetEntityLocation(entt::entity Entity)
    {
        return GetEntityTransform(Entity).GetWorldLocation();
    }

    void CWorld::SetEntityLocation(entt::entity Entity, FVector3 Location)
    {
        GetEntityTransform(Entity).SetLocation(Location);
    }

    void CWorld::SetEntityRotation(entt::entity Entity, FQuat Rotation)
    {
        GetEntityTransform(Entity).SetRotation(Rotation);
    }

    FVector3 CWorld::TranslateEntity(entt::entity Entity, FVector3 Translation)
    {
        return GetEntityTransform(Entity).Translate(Translation);
    }

    uint32 CWorld::GetNumEntities() const
    {
        return (uint32)EntityRegistry.view<entt::entity>().size();
    }

    void CWorld::SetActiveCamera(entt::entity InEntity) const
    {
        SetActiveCamera(InEntity, 0.0f);
    }

    void CWorld::SetActiveCamera(entt::entity InEntity, float BlendTime, ECameraBlendFunction Function) const
    {
        if (!EntityRegistry.valid(InEntity))
        {
            return;
        }

        if (EntityRegistry.all_of<SCameraComponent>(InEntity))
        {
            SCameraSystem::SetActiveCamera(const_cast<FEntityRegistry&>(EntityRegistry), InEntity, BlendTime, Function);
        }
    }

    SCameraComponent* CWorld::GetActiveCamera() const
    {
        return SCameraSystem::GetActiveCamera(const_cast<FEntityRegistry&>(EntityRegistry));
    }

    entt::entity CWorld::GetActiveCameraEntity() const
    {
        return SCameraSystem::GetActiveCameraEntity(const_cast<FEntityRegistry&>(EntityRegistry));
    }

    void CWorld::OnChangeCameraEvent(const FSwitchActiveCameraEvent& Event)
    {
        SetActiveCamera(Event.NewActiveEntity);
    }

    SDefaultWorldSettings& CWorld::GetDefaultWorldSettings()
    {
        if (!EntityRegistry.valid(SingletonEntity))
        {
            static SDefaultWorldSettings Defaults{};
            return Defaults;
        }
        
        return EntityRegistry.get_or_emplace<SDefaultWorldSettings>(SingletonEntity);
    }

    bool CWorld::EntityHasTag(entt::entity Entity, const FName& Tag)
    {
        if (auto Storage = EntityRegistry.storage(entt::hashed_string(Tag.c_str())))
        {
            return Storage->contains(Entity);
        }
        
        return false;
    }

    void CWorld::CreateRenderer()
    {
        // Headless process or dedicated-server world: no RHI / nothing to display. Leaving RenderScene
        // null makes Extract/Render skip this world (see ExtractWorlds/RenderWorlds and Extract()).
        if (!ShouldRender())
        {
            return;
        }

        if (!RenderScene)
        {
            RenderScene = MakeUnique<FForwardRenderScene>(this);
            RenderScene->Init();
            EntityRegistry.ctx().emplace<IRenderScene*>(RenderScene.get());
        }
    }

    void CWorld::DestroyRenderer()
    {
        if (RenderScene)
        {
            FlushRenderingCommands();

            RenderScene->Shutdown();
            RenderScene.reset();
        }
    }

    void CWorld::SetActive(bool bNewActive)
    {
        if (bActive != bNewActive)
        {
            bActive = bNewActive;

            if (bActive)
            {
                SuspendedTime = -1.0;
                CreateRenderer();
                RmlUi::SetActiveWorld(this);
            }
            else
            {
                DestroyRenderer();
            }
        }
    }

    bool CWorld::ReclaimIdleRenderer(double NowSeconds, double GraceSeconds)
    {
        if (bActive || RenderScene == nullptr)
        {
            return false;
        }

        if (SuspendedTime < 0.0)
        {
            // First frame observed idle: start the clock.
            SuspendedTime = NowSeconds;
            return false;
        }

        if (NowSeconds - SuspendedTime < GraceSeconds)
        {
            return false;
        }

        DestroyRenderer();
        return true;
    }

    ENetMode CWorld::GetNetMode() const
    {
        return OwningContext ? OwningContext->NetMode : ENetMode::Standalone;
    }

    bool CWorld::IsNetServer() const
    {
        return NetIsServerMode(GetNetMode());
    }

    int32 CWorld::GetConnectedClientCount() const
    {
        const FNetWorldState* State = EntityRegistry.ctx().find<FNetWorldState>();
        return State != nullptr ? State->ConnectedClients : 0;
    }

    bool CWorld::ShouldRender() const
    {
        // GetNetMode() is valid during InitializeWorld because CreateWorldContext sets OwningContext
        // (and its NetMode) before calling InitializeWorld -- keep that ordering.
        return !GIsHeadless && GetNetMode() != ENetMode::DedicatedServer;
    }

    CWorld* CWorld::DuplicateWorld(CWorld* OwningWorld)
    {
        CPackage* OuterPackage = OwningWorld->GetPackage();
        if (OuterPackage == nullptr)
        {
            return nullptr;
        }

        TVector<uint8> Data;
        FMemoryWriter Writer(Data);
        FObjectProxyArchiver WriterProxy(Writer, true);
        OwningWorld->Serialize(WriterProxy);
        
        FMemoryReader Reader(Data);
        FObjectProxyArchiver ReaderProxy(Reader, true);
        
        // Inherit the source world's name so logs/profilers identify
        // "NewWorld" rather than the auto-generated "CWorld_1".
        CWorld* PIEWorld = NewObject<CWorld>(/*Package*/ nullptr, OwningWorld->GetName(), FGuid::New(), OF_Transient);

        PIEWorld->PreLoad();
        PIEWorld->Serialize(ReaderProxy);
        PIEWorld->PostLoad();

        return PIEWorld;
    }

    const TVector<CWorld::FStageSlot>& CWorld::GetSystemsForUpdateStage(EUpdateStage Stage)
    {
        return SystemUpdateList[static_cast<uint32>(Stage)];
    }

    void CWorld::OnRelationshipComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        Registry.on_destroy<FRelationshipComponent>().disconnect<&CWorld::OnRelationshipComponentDestroyed>(this);
        ECS::Utils::RemoveFromParent(Registry, Entity);

        TVector<entt::entity> SubTree;
    
        auto CollectRecursive = [&](auto& Self, entt::entity Current) -> void
        {
            ECS::Utils::ForEachChild(Registry, Current, [&](entt::entity Child)
            {
                Self(Self, Child);
                SubTree.push_back(Child);
            });
        };
    
        CollectRecursive(CollectRecursive, Entity);

        for (int32 i = (int32)SubTree.size() - 1; i >= 0; i--)
        {
            if (Registry.valid(SubTree[i]))
            {
                Registry.destroy(SubTree[i]);
            }
        }
        
        Registry.on_destroy<FRelationshipComponent>().connect<&CWorld::OnRelationshipComponentDestroyed>(this);
    }

    void CWorld::OnTransformComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        STransformComponent& TransformComponent = Registry.get<STransformComponent>(Entity);
        TransformComponent.Registry = &EntityRegistry;
        TransformComponent.Entity = Entity;

        Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
    }

    void CWorld::OnWidgetComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        RmlUi::ReleaseWidget(this, Registry.get<SWidgetComponent>(Entity));
    }

    void CWorld::OnInputComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        // Bind the component to this world so its input queries resolve to this world's viewport.
        Registry.get<SInputComponent>(Entity).World = this;
    }

    void CWorld::OnInputComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // Release every action callback this entity registered so its captured Lua FRefs don't keep
        // the script env alive (and can't fire after the entity is gone).
        SInputComponent& Input = Registry.get<SInputComponent>(Entity);
        if (Input.ActionCallbackIds.empty())
        {
            return;
        }
        if (FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport())
        {
            for (uint64 Id : Input.ActionCallbackIds)
            {
                V->GetContext().UnregisterActionCallback(Id);
            }
        }
        Input.ActionCallbackIds.clear();
    }

    void CWorld::OnScriptComponentConstruct(entt::registry& Registry, entt::entity Entity)
    {
        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        OnScriptComponentCreated(Entity, ScriptComponent, true);
    }

    void CWorld::OnScriptComponentUpdated(entt::registry& Registry, entt::entity Entity)
    {
        // Fired by entt on_update when the component is patched -- on a client this is the replication "OnRep"
        // for a received SScriptComponent (NetReplication patches each applied component). Reattach only when
        // the ScriptPath actually changed, so a PropertyUpdate that re-sends an unchanged script is a no-op.
        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        const FStringView Path = ScriptComponent.ScriptPath.ResolvePath();

        // Already running exactly this script -> nothing to do.
        if (ScriptComponent.Script != nullptr &&
            FStringView(ScriptComponent.Script->Path.c_str(), ScriptComponent.Script->Path.size()) == Path)
        {
            return;
        }

        // Nothing attached and this exact path already failed to load -- don't retry every update.
        if (ScriptComponent.Script == nullptr)
        {
            if (FScriptAttachFailed* Failed = Registry.try_get<FScriptAttachFailed>(Entity))
            {
                if (FStringView(Failed->Path.c_str(), Failed->Path.size()) == Path)
                {
                    return;
                }
            }
        }

        // (Re)load from the now-current ScriptPath. Detaches the old script first if it was swapped.
        ReloadScriptForComponent(Entity, ScriptComponent);

        if (ScriptComponent.Script == nullptr && !Path.empty())
        {
            Registry.emplace_or_replace<FScriptAttachFailed>(Entity).Path.assign(Path.data(), Path.size());
            LOG_WARN("[Net] Replicated script '{}' failed to load on this peer -- marking failed (won't reload until the path changes or the entity respawns).",
                FString(Path.data(), Path.size()).c_str());
        }
        else
        {
            Registry.remove<FScriptAttachFailed>(Entity); // success (or cleared path) clears any prior failure
        }
    }

    void CWorld::SetupScriptComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        ScriptComponent.Entity = Entity;
        ScriptComponent.World  = this;
        
        const FStringView ResolvedScriptPath = ScriptComponent.ScriptPath.ResolvePath();
        if (ResolvedScriptPath.empty())
        {
            return;
        }

        ScriptComponent.Script = Lua::FScriptingContext::Get().LoadUniqueScriptPath(ResolvedScriptPath);
        if (ScriptComponent.Script == nullptr || !ScriptComponent.Script->Reference.IsValid())
        {
            return;
        }

        // Stable ptr: FScript is held by shared_ptr for the script's lifetime.
        ScriptComponent.Script->ThreadData.Entity = Entity;
        ScriptComponent.Script->ThreadData.World  = this;
        if (lua_State* ScriptThread = ScriptComponent.Script->Reference.GetState())
        {
            lua_setthreaddata(ScriptThread, &ScriptComponent.Script->ThreadData);
        }
        
        ScriptComponent.Script->Reference.RawSet("_Registry", &EntityRegistry);
        ScriptComponent.Script->Reference.RawSet("_Events",   &EntityRegistry.ctx().get<FLuaEventBus>());
        ScriptComponent.Script->Reference.RawSet("_Timers",   &EntityRegistry.ctx().get<FTimerManager>());
        ScriptComponent.Script->Reference.RawSet("_Draw",     DrawInterfaceRef);

        // Per-entity fields visible to the user as `self.*`.
        ScriptComponent.Script->Reference.RawSet("World",     this);
        ScriptComponent.Script->Reference.RawSet("Entity",    Entity);
        ScriptComponent.Script->Reference.RawSet("Transform", &EntityRegistry.get<STransformComponent>(Entity));
        ScriptComponent.Script->Reference.RawSet("Name",      EntityRegistry.get<SNameComponent>(Entity).Name);
        
        ScriptComponent.ScriptMetaTable     = ScriptComponent.Script->Reference["Meta"];
        ScriptComponent.AttachFunc          = ScriptComponent.Script->Reference["OnAttach"];
        ScriptComponent.ReadyFunc           = ScriptComponent.Script->Reference["OnReady"];
        ScriptComponent.UpdateFunc          = ScriptComponent.Script->Reference["OnUpdate"];
        ScriptComponent.DetachFunc          = ScriptComponent.Script->Reference["OnDetach"];

        // No base no-op fallback (see ScriptComponent.h): invalid FRef means "not defined".
        ScriptComponent.FixedUpdateFunc     = ScriptComponent.Script->Reference["OnFixedUpdate"];
        ScriptComponent.EditorUpdateFunc    = ScriptComponent.Script->Reference["OnEditorUpdate"];
        ScriptComponent.InputFunc           = ScriptComponent.Script->Reference["OnInput"];

        ScriptComponent.ContactBeginFunc    = ScriptComponent.Script->Reference["OnContactBegin"];
        ScriptComponent.ContactEndFunc      = ScriptComponent.Script->Reference["OnContactEnd"];
        ScriptComponent.OverlapBeginFunc    = ScriptComponent.Script->Reference["OnOverlapBegin"];
        ScriptComponent.OverlapEndFunc      = ScriptComponent.Script->Reference["OnOverlapEnd"];

        // Install RPC dispatch wrappers over --@rpc methods (captures originals for receive-side invoke).
        Net::WrapScriptRpcs(ScriptComponent.Script.get());
        
        if (ScriptComponent.Script->ExportsSchema.IsValid())
        {
            Lua::ReconcileOverrides(
                ScriptComponent.Script->ExportsSchema,
                ScriptComponent.Script->ExportDefaults,
                ScriptComponent.PropertyOverrides.Items);

            if (lua_State* ScriptState = ScriptComponent.Script->Reference.GetState())
            {
                ScriptComponent.Script->Reference.Push();
                Lua::ApplyOverridesToScriptTable(
                    ScriptState, -1,
                    ScriptComponent.Script->ExportsSchema,
                    ScriptComponent.PropertyOverrides.Items);
                lua_pop(ScriptState, 1);
            }
        }
        
        if (ScriptComponent.ScriptMetaTable.IsValid())
        {
            auto MaybeTickRate = ScriptComponent.ScriptMetaTable.Get<float>("TickRate");
            ScriptComponent.TickRate = MaybeTickRate ? MaybeTickRate.value() : 0.0f;
            
            auto MaybeUpdateStage = ScriptComponent.ScriptMetaTable.Get<EUpdateStage>("UpdateStage");
            ScriptComponent.UpdateStage = MaybeUpdateStage ? MaybeUpdateStage.value() : EUpdateStage::PrePhysics;
        }
        
        if (EntityRegistry.any_of<
                FUpdateStage_FrameStart,
                FUpdateStage_PrePhysics,
                FUpdateStage_DuringPhysics,
                FUpdateStage_PostPhysics,
                FUpdateStage_FrameEnd,
                FUpdateStage_Paused>(Entity))
        {
            EntityRegistry.remove<FUpdateStage_FrameStart>(Entity);
            EntityRegistry.remove<FUpdateStage_PrePhysics>(Entity);
            EntityRegistry.remove<FUpdateStage_DuringPhysics>(Entity);
            EntityRegistry.remove<FUpdateStage_PostPhysics>(Entity);
            EntityRegistry.remove<FUpdateStage_FrameEnd>(Entity);
            EntityRegistry.remove<FUpdateStage_Paused>(Entity);
        }

        switch (ScriptComponent.UpdateStage)
        {
        case EUpdateStage::FrameStart:
            EntityRegistry.emplace_or_replace<FUpdateStage_FrameStart>(Entity);
            break;
        case EUpdateStage::PrePhysics:
            EntityRegistry.emplace_or_replace<FUpdateStage_PrePhysics>(Entity);
            break;
        case EUpdateStage::DuringPhysics:
            EntityRegistry.emplace_or_replace<FUpdateStage_DuringPhysics>(Entity);
            break;
        case EUpdateStage::PostPhysics:
            EntityRegistry.emplace_or_replace<FUpdateStage_PostPhysics>(Entity);
            break;
        case EUpdateStage::FrameEnd:
            EntityRegistry.emplace_or_replace<FUpdateStage_FrameEnd>(Entity);
            break;
        case EUpdateStage::Paused:
            EntityRegistry.emplace_or_replace<FUpdateStage_Paused>(Entity);
            break;
        case EUpdateStage::Max:
            break;
        }

        if (ScriptComponent.UpdateFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasUpdateFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasUpdateFn>(Entity);

        if (ScriptComponent.FixedUpdateFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasFixedUpdateFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasFixedUpdateFn>(Entity);

        if (ScriptComponent.EditorUpdateFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasEditorUpdateFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasEditorUpdateFn>(Entity);

        if (ScriptComponent.InputFunc.IsValid())
            EntityRegistry.emplace_or_replace<FScriptHasInputFn>(Entity);
        else
            EntityRegistry.remove<FScriptHasInputFn>(Entity);
    }

    void CWorld::OnScriptComponentCreated(entt::entity Entity, SScriptComponent& ScriptComponent, bool bRunReady)
    {
        SetupScriptComponent(Entity, ScriptComponent);

        if (ScriptComponent.Script == nullptr)
        {
            return;
        }

        if (!IsScriptActiveInWorld(Entity))
        {
            return;
        }

        if (ScriptComponent.AttachFunc.IsValid())
        {
            ScriptComponent.Script->PublishThreadContext();
            // Coroutine so OnAttach can yield (wait/await) and resume later.
            ScriptComponent.Script->InvokeAsCoroutine(ScriptComponent.AttachFunc, ScriptComponent.Script->Reference);
        }

        if (bRunReady)
        {
            SingletonDispatcher.enqueue<FScriptComponentPendingReady>(Entity);
        }
    }

    void CWorld::OnScriptComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        // Auto-remove all event bus subscriptions owned by this entity.
        Registry.ctx().get<FLuaEventBus>().UnsubscribeEntity(Entity);
        Registry.ctx().get<FTimerManager>().ClearTimersForEntity(Entity);

        SScriptComponent& ScriptComponent = Registry.get<SScriptComponent>(Entity);
        if (ScriptComponent.Script == nullptr || !ScriptComponent.DetachFunc.IsValid())
        {
            return;
        }

        if (IsScriptActiveInWorld(Entity))
        {
            ScriptComponent.Script->PublishThreadContext();
            ScriptComponent.DetachFunc.Call(ScriptComponent.Script->Reference);
        }
    }

    void CWorld::ReloadScriptForComponent(entt::entity Entity, SScriptComponent& ScriptComponent)
    {
        if (ScriptComponent.Script != nullptr && ScriptComponent.DetachFunc.IsValid())
        {
            if (IsScriptActiveInWorld(Entity))
            {
                ScriptComponent.Script->PublishThreadContext();
                ScriptComponent.DetachFunc.Call(ScriptComponent.Script->Reference);
            }
        }

        EntityRegistry.ctx().get<FLuaEventBus>().UnsubscribeEntity(Entity);
        EntityRegistry.ctx().get<FTimerManager>().ClearTimersForEntity(Entity);

        ScriptComponent.Script.reset();
        ScriptComponent.AttachFunc          = {};
        ScriptComponent.ReadyFunc           = {};
        ScriptComponent.UpdateFunc          = {};
        ScriptComponent.DetachFunc          = {};
        ScriptComponent.FixedUpdateFunc     = {};
        ScriptComponent.EditorUpdateFunc    = {};
        ScriptComponent.ContactBeginFunc    = {};
        ScriptComponent.ContactEndFunc      = {};
        ScriptComponent.OverlapBeginFunc    = {};
        ScriptComponent.OverlapEndFunc      = {};
        ScriptComponent.ScriptMetaTable     = {};
        ScriptComponent.MessageHandlers.clear();

        OnScriptComponentCreated(Entity, ScriptComponent, /*bRunReady*/ true);
    }

    void CWorld::SetEntityScript(entt::entity Entity, FStringView Path)
    {
        if (!EntityRegistry.valid(Entity))
        {
            return;
        }

        SScriptComponent& ScriptComponent = EntityRegistry.get_or_emplace<SScriptComponent>(Entity);

        // Already running this exact script -> nothing to do (avoids a needless detach/reattach).
        if (ScriptComponent.Script != nullptr &&
            FStringView(ScriptComponent.Script->Path.c_str(), ScriptComponent.Script->Path.size()) == Path)
        {
            return;
        }

        ScriptComponent.ScriptPath.SetPath(Path);
        ReloadScriptForComponent(Entity, ScriptComponent); // detaches any prior script, attaches the new one

        // On the authority, a networked entity's script change must reach clients: flag it dirty so the next
        // tick sends a PropertyUpdate carrying the new (replicated) ScriptPath, which clients swap to.
        if (NetIsServerMode(GetNetMode()) && EntityRegistry.all_of<SNetworkComponent>(Entity))
        {
            EntityRegistry.emplace_or_replace<FNetDirty>(Entity);
        }
    }

    void CWorld::OnScriptSourceReloaded(FStringView Path)
    {
        // Snapshot the matching entities BEFORE reloading: ReloadScriptForComponent synchronously calls the
        // script's Lua OnDetach/OnAttach, which can create/destroy entities or add/remove SScriptComponent --
        // mutating the pool we'd otherwise be iterating (iterator invalidation + dangling component ref).
        TVector<entt::entity> ToReload;
        auto View = EntityRegistry.view<SScriptComponent>();
        for (entt::entity Entity : View)
        {
            if (View.get<SScriptComponent>(Entity).ScriptPath.ResolvePath() == Path)
            {
                ToReload.push_back(Entity);
            }
        }

        for (entt::entity Entity : ToReload)
        {
            // A prior reload's hook may have destroyed this entity or removed its script; re-check + re-fetch.
            if (!EntityRegistry.valid(Entity))
            {
                continue;
            }
            if (SScriptComponent* Component = EntityRegistry.try_get<SScriptComponent>(Entity))
            {
                ReloadScriptForComponent(Entity, *Component);

                if (NetIsServerMode(GetNetMode()) && EntityRegistry.all_of<SNetworkComponent>(Entity))
                {
                    EntityRegistry.remove<FScriptRepState>(Entity);
                    EntityRegistry.emplace_or_replace<FNetDirty>(Entity);
                }
            }
        }

        // Queue any assigned script system using this path for a deferred re-bind (handled next frame in
        // ApplyPendingSystemChanges, off the tick path).
        for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
        {
            if (FStringView(Instance->Path.c_str()) == Path)
            {
                if (eastl::find(ScriptSystemsToReload.begin(), ScriptSystemsToReload.end(), Instance->Path) == ScriptSystemsToReload.end())
                {
                    ScriptSystemsToReload.push_back(Instance->Path);
                }
                bSystemsDirty = true;
            }
        }
    }

    void CWorld::RegisterSystems()
    {
        for (int i = 0; i < (int)EUpdateStage::Max; ++i)
        {
            SystemUpdateList[i].clear();
        }
        ActiveSystems.clear();

        for (const FNativeSystemDesc& Desc : FSystemRegistry::Get().GetNativeSystems())
        {
            // Skip systems disabled for this world; they are never started or ticked. A stale disabled
            // name (system since renamed/removed) simply matches nothing here.
            if (!Desc.Name.IsNone() && DisabledSystems.count(Desc.Name))
            {
                continue;
            }

            // Schedule a slot per enabled stage that actually has an Update; a null Update is never
            // scheduled (a Startup/Teardown-only system still ticks in no stage).
            bool bAnyStage = false;
            for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
            {
                if (!Desc.Priorities.IsStageEnabled((EUpdateStage)i))
                {
                    continue;
                }

                bAnyStage = true;
                if (Desc.Update != nullptr)
                {
                    SystemUpdateList[i].push_back(FStageSlot{ Desc.Update, nullptr, Desc.Access, Desc.Priorities.GetPriorityForStage((EUpdateStage)i) });
                }
            }

            // A system is "active" (owns Startup/Teardown) if it participates in at least one stage.
            if (bAnyStage)
            {
                FActiveSystem& Active = ActiveSystems.emplace_back();
                Active.Name     = Desc.Name;
                Active.Hash     = Desc.Hash;
                Active.Startup  = Desc.Startup;
                Active.Teardown = Desc.Teardown;
                Active.Self     = nullptr;
                Active.bScript  = false;
            }
        }

        // Bring the live script-system instances in line with the assigned paths, then schedule them. Script
        // systems are always exclusive (the Lua VM is single-threaded), so they never batch in parallel.
        ReconcileScriptSystemInstances();

        for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
        {
            if (!Instance->Name.IsNone() && DisabledSystems.count(Instance->Name))
            {
                continue;
            }

            for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
            {
                if (Instance->Priorities.IsStageEnabled((EUpdateStage)i))
                {
                    SystemUpdateList[i].push_back(FStageSlot{ &ScriptSystem_Update, Instance.get(), FSystemAccess::Exclusive(), Instance->Priorities.GetPriorityForStage((EUpdateStage)i) });
                }
            }

            FActiveSystem& Active = ActiveSystems.emplace_back();
            Active.Name     = Instance->Name;
            Active.Hash     = Hash::FNV1a::GetHash64(Instance->Path.c_str());
            Active.Startup  = &ScriptSystem_Startup;
            Active.Teardown = &ScriptSystem_Teardown;
            Active.Self     = Instance.get();
            Active.bScript  = true;
        }

        // Lower value = higher priority (Highest=0 .. Low=192), so ascending runs Highest first.
        for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
        {
            eastl::sort(SystemUpdateList[i].begin(), SystemUpdateList[i].end(),
                [](const FStageSlot& A, const FStageSlot& B) { return A.StagePriority < B.StagePriority; });
        }
    }

    void CWorld::ReconcileScriptSystemInstances()
    {
        // Drop instances whose path is no longer assigned.
        for (auto It = ScriptSystemInstances.begin(); It != ScriptSystemInstances.end(); )
        {
            const bool bStillAssigned = eastl::find(ScriptSystems.begin(), ScriptSystems.end(), (*It)->Path) != ScriptSystems.end();
            if (bStillAssigned)
            {
                ++It;
            }
            else
            {
                It = ScriptSystemInstances.erase(It);
            }
        }

        // Load instances for newly assigned paths (keeping existing ones pointer-stable).
        for (const FString& Path : ScriptSystems)
        {
            bool bExists = false;
            for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
            {
                if (Instance->Path == Path)
                {
                    bExists = true;
                    break;
                }
            }

            if (!bExists)
            {
                if (TUniquePtr<FScriptSystemInstance> Instance = LoadScriptSystem(this, FStringView(Path.c_str())))
                {
                    ScriptSystemInstances.push_back(eastl::move(Instance));
                }
            }
        }
    }

    void CWorld::SyncScriptSystemSettings()
    {
        SDefaultWorldSettings& Settings = GetDefaultWorldSettings();
        Settings.ScriptSystems.clear();
        for (const FString& Path : PendingScriptSystems)
        {
            Settings.ScriptSystems.push_back(Path);
        }
    }

    void CWorld::AddScriptSystem(FStringView Path)
    {
        FString NewPath(Path.data(), Path.size());
        if (eastl::find(PendingScriptSystems.begin(), PendingScriptSystems.end(), NewPath) != PendingScriptSystems.end())
        {
            return;
        }

        PendingScriptSystems.push_back(NewPath);
        SyncScriptSystemSettings();
        bSystemsDirty = true;
    }

    void CWorld::RemoveScriptSystem(FStringView Path)
    {
        FString TargetPath(Path.data(), Path.size());
        auto It = eastl::find(PendingScriptSystems.begin(), PendingScriptSystems.end(), TargetPath);
        if (It == PendingScriptSystems.end())
        {
            return;
        }

        PendingScriptSystems.erase(It);
        SyncScriptSystemSettings();
        bSystemsDirty = true;
    }

    void CWorld::GetScriptSystems(TVector<FScriptSystemInfo>& Out) const
    {
        Out.clear();
        for (const FString& Path : PendingScriptSystems)
        {
            FScriptSystemInfo& Info = Out.emplace_back();
            Info.Path = Path;

            // Fill name + stages from the live instance if one is loaded for this path.
            for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
            {
                if (Instance->Path == Path)
                {
                    Info.Name = Instance->Name;
                    for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
                    {
                        if (Instance->Priorities.IsStageEnabled((EUpdateStage)i))
                        {
                            Info.Stages.push_back((EUpdateStage)i);
                        }
                    }
                    break;
                }
            }
        }
    }

    void CWorld::ApplyPendingSystemChanges()
    {
        if (!bSystemsDirty)
        {
            return;
        }
        bSystemsDirty = false;

        const bool bDisabledChanged = (PendingDisabledSystems != DisabledSystems);
        const bool bScriptsChanged  = (PendingScriptSystems != ScriptSystems);
        const TVector<FString> ReloadPaths = ScriptSystemsToReload;
        ScriptSystemsToReload.clear();

        if (!bDisabledChanged && !bScriptsChanged && ReloadPaths.empty())
        {
            return;
        }

        // Snapshot the currently-active unique systems so we can tell which are newly removed/added. Script
        // systems hash by their (stable) path, so a persisting script keeps its hash across the rebuild.
        THashSet<uint64> BeforeHashes;
        ForEachUniqueSystem([&](const FActiveSystem& System)
        {
            BeforeHashes.insert(System.Hash);
        });

        // Teardown native systems about to be disabled while their entries are still live (before rebuild).
        ForEachUniqueSystem([&](const FActiveSystem& System)
        {
            if (!System.bScript && !System.Name.IsNone() && PendingDisabledSystems.count(System.Name) && !DisabledSystems.count(System.Name))
            {
                if (System.Teardown)
                {
                    System.Teardown(System.Self, SystemContext);
                }
            }
        });

        // Teardown script systems whose path is being removed, while their instances are still live.
        for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
        {
            const bool bStillAssigned = eastl::find(PendingScriptSystems.begin(), PendingScriptSystems.end(), Instance->Path) != PendingScriptSystems.end();
            if (!bStillAssigned)
            {
                ScriptSystem_Teardown(Instance.get(), SystemContext);
            }
        }

        // Hot-reloaded script systems (path persists): teardown + re-resolve hooks from disk in place.
        for (const FString& Path : ReloadPaths)
        {
            if (eastl::find(PendingScriptSystems.begin(), PendingScriptSystems.end(), Path) == PendingScriptSystems.end())
            {
                continue;   // also being removed; the removal teardown above handled it
            }
            for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
            {
                if (Instance->Path == Path)
                {
                    ScriptSystem_Teardown(Instance.get(), SystemContext);
                    ReloadScriptSystem(*Instance);
                    break;
                }
            }
        }

        DisabledSystems = PendingDisabledSystems;
        ScriptSystems   = PendingScriptSystems;
        RegisterSystems();

        // Startup systems that are newly present (were not active before the rebuild).
        ForEachUniqueSystem([&](const FActiveSystem& System)
        {
            if (BeforeHashes.count(System.Hash) == 0)
            {
                if (System.Startup)
                {
                    System.Startup(System.Self, SystemContext);
                }
            }
        });

        // Restart hot-reloaded script systems: their path-hash was already present, so the diff skipped them.
        for (const FString& Path : ReloadPaths)
        {
            if (eastl::find(ScriptSystems.begin(), ScriptSystems.end(), Path) == ScriptSystems.end())
            {
                continue;
            }
            for (const TUniquePtr<FScriptSystemInstance>& Instance : ScriptSystemInstances)
            {
                if (Instance->Path == Path)
                {
                    ScriptSystem_Startup(Instance.get(), SystemContext);
                    break;
                }
            }
        }
    }

    void CWorld::GetAllSystems(TVector<FSystemInfo>& Out) const
    {
        Out.clear();

        for (const FNativeSystemDesc& Desc : FSystemRegistry::Get().GetNativeSystems())
        {
            if (Desc.Name.IsNone())
            {
                continue;
            }

            FSystemInfo& Info = Out.emplace_back();
            Info.Name     = Desc.Name;
            Info.bEnabled = PendingDisabledSystems.count(Info.Name) == 0;

            for (uint8 i = 0; i < (uint8)EUpdateStage::Max; ++i)
            {
                if (Desc.Priorities.IsStageEnabled((EUpdateStage)i))
                {
                    Info.Stages.push_back((EUpdateStage)i);
                }
            }
        }
    }

    bool CWorld::IsSystemEnabled(FName System) const
    {
        return PendingDisabledSystems.count(System) == 0;
    }

    void CWorld::SetSystemEnabled(FName System, bool bEnabled)
    {
        if (System.IsNone())
        {
            return;
        }

        const bool bCurrentlyEnabled = PendingDisabledSystems.count(System) == 0;
        if (bCurrentlyEnabled == bEnabled)
        {
            return;
        }

        if (bEnabled)
        {
            PendingDisabledSystems.erase(System);
        }
        else
        {
            PendingDisabledSystems.insert(System);
        }

        // Persist immediately into the world-settings component (safe to mutate; not the live system list).
        // The actual system-list rebuild is deferred to the next frame via ApplyPendingSystemChanges.
        SDefaultWorldSettings& Settings = GetDefaultWorldSettings();
        Settings.DisabledSystems.clear();
        for (const FName& Name : PendingDisabledSystems)
        {
            Settings.DisabledSystems.push_back(Name);
        }

        bSystemsDirty = true;
    }

    void CWorld::DrawBillboard(FRHIImage* Image, const FVector3& Location, float Scale)
    {
        if (RenderScene == nullptr)
        {
            return;
        }
        RenderScene->DrawBillboard(Image, Location, Scale);
    }

    void CWorld::DrawLine(const FVector3& Start, const FVector3& End, const FVector4& Color, float Thickness, bool bDepthTest, float Duration)
    {
        if (IsSuspended())
        {
            return;
        }
        
        LineBatcherComponent->EnqueueLine(Start, End, Color, Thickness, bDepthTest, Duration);
    }

    void CWorld::DrawSolidTriangles(TVector<FSimpleElementVertex>&& Vertices, bool bDepthTest, float Duration)
    {
        if (IsSuspended())
        {
            return;
        }

        TriangleBatcherComponent->EnqueueTriangles(std::move(Vertices), bDepthTest, Duration);
    }

    void CWorld::DrawDebugText(const FString& Text, const FVector4& Color)
    {
#if !defined(LE_SHIPPING)
        if (IsSuspended())
        {
            return;
        }

        FDebugTextLine& Line = DebugTextLines.emplace_back();
        Line.Text  = Text;
        Line.Color = Color;
#endif
    }

    void CWorld::DrainDebugTextLines(TVector<FDebugTextLine>& Out)
    {
        Out = Move(DebugTextLines);
        DebugTextLines.clear();
    }

    TOptional<SRayResult> CWorld::CastRay(const SRayCastSettings& Settings)
    {
        LUMINA_PROFILE_SCOPE();
        
        if (PhysicsScene == nullptr)
        {
            return eastl::nullopt;
        }
        
        TOptional<SRayResult> Result = PhysicsScene->CastRay(Settings);
        
        if (Settings.bDrawDebug)
        {
            if (Result.has_value())
            {
                SRayResult RayResult = Result.value();
                DrawLine(Settings.Start, RayResult.Location, FColor(Settings.DebugMissColor), 3.0f, true, Settings.DebugDuration);
                
                FVector3 NormalEnd = RayResult.Location + RayResult.Normal * 0.5f;
                DrawLine(RayResult.Location, NormalEnd, FColor::Blue, 3.0f,true, Settings.DebugDuration);
                
                DrawBox(RayResult.Location, FVector3(0.05f), FQuat(1.0f, 0.0f, 0.0f, 0.0f), FColor::Yellow, 3.0, true, Settings.DebugDuration);
                
                DrawLine(RayResult.Location, Settings.End, FColor(Settings.DebugHitColor), 3.0f, true, Settings.DebugDuration);
            }
            else
            {
                DrawLine(Settings.Start, Settings.End, FColor(Settings.DebugMissColor), 3.0f, true, Settings.DebugDuration);
            }
        }
        
        return Move(Result);
    }
    
    TVector<SRayResult> CWorld::CastSphere(const SSphereCastSettings& Settings) const
    {
        LUMINA_PROFILE_SCOPE();

        if (PhysicsScene == nullptr)
        {
            return {};
        }
        
        return PhysicsScene->CastSphere(Settings);
        
        
    }

    EUpdateStage CWorld::GetUpdateStage() const
    {
        return SystemContext.GetUpdateStage();
    }

    entt::entity CWorld::GetEntityByTag(const FName& Tag)
    {
        auto& Storage = EntityRegistry.storage<STagComponent>(entt::hashed_string(Tag.c_str()));
        if (Storage.empty())
        {
            return entt::null;
        }
        
        return *Storage.data();
    }

    entt::entity CWorld::GetEntityByName(const FName& Name)
    {
        auto View = EntityRegistry.view<SNameComponent>();
        for (entt::entity Entity : View)
        {
            SNameComponent& NameComponent = View.get<SNameComponent>(Entity);
            if (NameComponent.Name == Name)
            {
                return Entity;
            }
        }
        
        return entt::null;
    }

    entt::entity CWorld::GetFirstEntityWith(entt::id_type Type)
    {
        if (!EntityRegistry.storage(Type))
        {
            return entt::null;
        }

        auto storage = EntityRegistry.storage(Type);

        if (storage->empty())
        {
            return entt::null;
        }
        return *storage->data();
    }

    void CWorld::SetEntityTransform(entt::entity Entity, const FTransform& NewTransform)
    {
        EntityRegistry.emplace_or_replace<STransformComponent>(Entity, NewTransform);
        EntityRegistry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
    }

    TVector<entt::entity> CWorld::GetSelectedEntities() const
    {
        auto View = EntityRegistry.view<FSelectedInEditorComponent>();
        TVector<entt::entity> Selections(View.size());
        View.each([&](entt::entity Entity)
        {
           Selections.push_back(Entity); 
        });
        return Selections;
    }

    bool CWorld::IsSelected(entt::entity Entity) const
    {
        return EntityRegistry.any_of<FSelectedInEditorComponent>(Entity);
    }

    void CWorld::TickSystems(FSystemContext& Context)
    {
        TVector<FStageSlot>& Systems = SystemUpdateList[(uint32)Context.GetUpdateStage()];
        const size_t Count = Systems.size();

        auto RunOne = [&](FStageSlot& S) { S.Update(S.Self, Context); };

        // Walk the priority-sorted list and greedily group consecutive systems that conflict with
        // NOTHING already in the current batch; flush the batch (run concurrently) on the first
        // conflict, then continue. Exclusive systems (no declared access) always run alone. A
        // conflicting/lower-priority system lands in a later batch, so priority order is preserved.
        size_t i = 0;
        while (i < Count)
        {
            size_t j = i + 1;
            if (!Systems[i].Access.bExclusive)
            {
                while (j < Count)
                {
                    bool ConflictsWithBatch = Systems[j].Access.bExclusive;
                    for (size_t k = i; !ConflictsWithBatch && k < j; ++k)
                    {
                        ConflictsWithBatch = FSystemAccess::Conflicts(Systems[k].Access, Systems[j].Access);
                    }
                    if (ConflictsWithBatch)
                    {
                        break;
                    }
                    ++j;
                }
            }

            const size_t BatchSize = j - i;
            if (BatchSize <= 1)
            {
                RunOne(Systems[i]);
            }
            else
            {
                // One system per job; the game thread assist-waits, nested ParallelFor inside a system
                // nests fine on fibers.
                Task::ParallelFor(static_cast<uint32>(BatchSize), [&](uint32 Index)
                {
                    DEBUG_ASSERT(!Systems[i + Index].Access.bExclusive); // scheduler invariant
                    RunOne(Systems[i + Index]);
                }, 1);
            }

            i = j;
        }
    }
}
