#include "Platform/GenericPlatform.h"
#include "Containers/String.h"
#include "Containers/Name.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Systems/SystemContext.h"
#include "World/Entity/Components/CSharpScriptComponent.h"
#include "Scripting/DotNet/DotNetHost.h"

// Hand-written native -> C# gameplay bindings: the World / Physics / Debug / Net surface a scripter needs
// but the Reflector can't auto-generate (CWorld and IPhysicsScene gameplay calls aren't reflected
// FUNCTION()s). Each export is `extern "C" RUNTIME_API LuminaSharp_<Domain>_<Op>` and is resolved on the
// C# side by a `delegate*` field (NativeBindings.Resolve), mirroring the Lua World.* / World.Physics:*
// facades. The matching C# facades live in LuminaSharp/Gameplay/{World,Physics,DebugDraw,Net}.cs. World is
// an opaque CWorld* (uint64), Entity an entt id (uint32) - the same convention the component ops use.
// Game thread only.

using namespace Lumina;

namespace
{
    FORCEINLINE CWorld* AsWorld(uint64 Handle) { return reinterpret_cast<CWorld*>(Handle); }
    FORCEINLINE entt::entity AsEntity(uint32 Entity) { return static_cast<entt::entity>(Entity); }
    FORCEINLINE uint32 ToId(entt::entity Entity) { return static_cast<uint32>(Entity); }
}

// Blittable raycast result mirrored by LuminaSharp.RaycastHit's wire struct. bHit == 0 means no hit.
struct FLmRayHit
{
    int32    bHit;
    uint32   Entity;
    int64    BodyID;
    FVector3 Location;
    FVector3 Normal;
    float    Distance;
    float    Fraction;
};

//================================================================================================
// World (FName-keyed spawn/find + ECS::Utils rotation/scale. The entity transform/time/destroy/count
// methods are GENERATED from CWorld's FUNCTION(Script) declarations now that the Reflector marshals
// entt::entity - see World.generated.{cpp,cs}. Only the not-yet-reflectable surface lives here.)
//================================================================================================

extern "C" RUNTIME_API uint32 LuminaSharp_World_CreateEntity(uint64 World, const char* Name, int32 Length)
{
    CWorld* W = AsWorld(World);
    if (W == nullptr)
    {
        return ToId(entt::null);
    }
    const FName EntityName = (Length > 0) ? FName(Name, (size_t)Length) : FName("Entity");
    return ToId(W->ConstructEntity(EntityName));
}

extern "C" RUNTIME_API int32 LuminaSharp_World_IsValidEntity(uint64 World, uint32 Entity)
{
    CWorld* W = AsWorld(World);
    return (W != nullptr && W->GetEntityRegistry().valid(AsEntity(Entity))) ? 1 : 0;
}

extern "C" RUNTIME_API FQuat LuminaSharp_World_GetRotation(uint64 World, uint32 Entity)
{
    CWorld* W = AsWorld(World);
    return W ? ECS::Utils::GetEntityRotation(W->GetEntityRegistry(), AsEntity(Entity)) : FQuat();
}

// Script-to-script: hands back the managed GCHandle (as void*) of the C# EntityScript bound to an entity,
// so a script can fetch ANOTHER entity's script as its concrete type and call its public methods directly.
// C# resolves it with GCHandle.FromIntPtr(...).Target. Null when the entity has no bound script instance.
extern "C" RUNTIME_API void* LuminaSharp_GetEntityScriptHandle(uint64 World, uint32 Entity)
{
    CWorld* W = AsWorld(World);
    if (W == nullptr)
    {
        return nullptr;
    }
    const SCSharpScriptComponent* Script = W->GetEntityRegistry().try_get<SCSharpScriptComponent>(AsEntity(Entity));
    // Only hand back a CURRENT-generation, bound handle. A stale (pre-hot-reload) Instance points at a
    // GCHandle managed already freed on unload, so resolving it in C# would be a use-after-free. This is the
    // same generation guard every other Component.Instance consumer uses (the on_destroy hook, SetEntityScript,
    // physics collision dispatch). A merely-disabled script is never refreshed by the bind pass, so without
    // this its Instance would dangle after a reload.
    if (Script == nullptr || Script->Instance == nullptr || Script->Generation != DotNet::GetScriptGeneration())
    {
        return nullptr;
    }
    return Script->Instance;
}

extern "C" RUNTIME_API FVector3 LuminaSharp_World_GetScale(uint64 World, uint32 Entity)
{
    CWorld* W = AsWorld(World);
    return W ? ECS::Utils::GetEntityScale(W->GetEntityRegistry(), AsEntity(Entity)) : FVector3(1.0f);
}

extern "C" RUNTIME_API void LuminaSharp_World_SetScale(uint64 World, uint32 Entity, FVector3 Scale)
{
    if (CWorld* W = AsWorld(World))
    {
        ECS::Utils::SetEntityScale(W->GetEntityRegistry(), AsEntity(Entity), Scale);
    }
}

extern "C" RUNTIME_API void LuminaSharp_World_SetActiveCamera(uint64 World, uint32 Entity)
{
    if (CWorld* W = AsWorld(World))
    {
        W->SetActiveCamera(AsEntity(Entity));
    }
}

//================================================================================================
// Physics (entity-keyed; the scene resolves the body)
//================================================================================================

extern "C" RUNTIME_API FLmRayHit LuminaSharp_Physics_Raycast(uint64 World, FVector3 Start, FVector3 End, uint32 IgnoreEntity)
{
    FLmRayHit Hit{};
    CWorld* W = AsWorld(World);
    if (W == nullptr)
    {
        return Hit;
    }

    SRayCastSettings Settings;
    Settings.Start = Start;
    Settings.End = End;
    if (IgnoreEntity != ToId(entt::null))
    {
        if (Physics::IPhysicsScene* Scene = W->GetPhysicsScene())
        {
            const uint32 BodyID = Scene->GetEntityBodyID(AsEntity(IgnoreEntity));
            if (BodyID != 0xFFFFFFFFu)
            {
                Settings.IgnoreBodies.push_back(BodyID);
            }
        }
    }

    TOptional<SRayResult> Result = W->CastRay(Settings);
    if (Result.has_value())
    {
        const SRayResult& R = Result.value();
        Hit.bHit = 1;
        Hit.Entity = R.Entity;
        Hit.BodyID = R.BodyID;
        Hit.Location = R.Location;
        Hit.Normal = R.Normal;
        Hit.Distance = R.Distance;
        Hit.Fraction = R.Fraction;
    }
    return Hit;
}

namespace
{
    FORCEINLINE Physics::IPhysicsScene* SceneOf(uint64 World)
    {
        CWorld* W = AsWorld(World);
        return W ? W->GetPhysicsScene() : nullptr;
    }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_AddForce(uint64 World, uint32 Entity, FVector3 Force)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->AddForce(AsEntity(Entity), Force); }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_AddImpulse(uint64 World, uint32 Entity, FVector3 Impulse)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->AddImpulse(AsEntity(Entity), Impulse); }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_AddTorque(uint64 World, uint32 Entity, FVector3 Torque)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->AddTorque(AsEntity(Entity), Torque); }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_AddAngularImpulse(uint64 World, uint32 Entity, FVector3 AngularImpulse)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->AddAngularImpulse(AsEntity(Entity), AngularImpulse); }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_AddForceAtPosition(uint64 World, uint32 Entity, FVector3 Force, FVector3 Position)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->AddForceAtPosition(AsEntity(Entity), Force, Position); }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_AddImpulseAtPosition(uint64 World, uint32 Entity, FVector3 Impulse, FVector3 Position)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->AddImpulseAtPosition(AsEntity(Entity), Impulse, Position); }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_SetLinearVelocity(uint64 World, uint32 Entity, FVector3 Velocity)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->SetLinearVelocity(AsEntity(Entity), Velocity); }
}

extern "C" RUNTIME_API FVector3 LuminaSharp_Physics_GetLinearVelocity(uint64 World, uint32 Entity)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetLinearVelocity(AsEntity(Entity)) : FVector3(0.0f);
}

extern "C" RUNTIME_API void LuminaSharp_Physics_SetAngularVelocity(uint64 World, uint32 Entity, FVector3 Velocity)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->SetAngularVelocity(AsEntity(Entity), Velocity); }
}

extern "C" RUNTIME_API FVector3 LuminaSharp_Physics_GetAngularVelocity(uint64 World, uint32 Entity)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetAngularVelocity(AsEntity(Entity)) : FVector3(0.0f);
}

extern "C" RUNTIME_API FVector3 LuminaSharp_Physics_GetBodyPosition(uint64 World, uint32 Entity)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetBodyPosition(AsEntity(Entity)) : FVector3(0.0f);
}

extern "C" RUNTIME_API FQuat LuminaSharp_Physics_GetBodyRotation(uint64 World, uint32 Entity)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetBodyRotation(AsEntity(Entity)) : FQuat();
}

extern "C" RUNTIME_API FVector3 LuminaSharp_Physics_GetVelocityAtPoint(uint64 World, uint32 Entity, FVector3 Point)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetVelocityAtPoint(AsEntity(Entity), Point) : FVector3(0.0f);
}

extern "C" RUNTIME_API FVector3 LuminaSharp_Physics_GetCenterOfMass(uint64 World, uint32 Entity)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetCenterOfMass(AsEntity(Entity)) : FVector3(0.0f);
}

extern "C" RUNTIME_API void LuminaSharp_Physics_SetGravityFactor(uint64 World, uint32 Entity, float Factor)
{
    if (Physics::IPhysicsScene* S = SceneOf(World)) { S->SetGravityFactor(AsEntity(Entity), Factor); }
}

extern "C" RUNTIME_API uint32 LuminaSharp_Physics_GetBodyId(uint64 World, uint32 Entity)
{
    Physics::IPhysicsScene* S = SceneOf(World);
    return S ? S->GetEntityBodyID(AsEntity(Entity)) : 0xFFFFFFFFu;
}

extern "C" RUNTIME_API void LuminaSharp_Physics_ActivateBody(uint64 World, uint32 Entity)
{
    if (Physics::IPhysicsScene* S = SceneOf(World))
    {
        const uint32 BodyID = S->GetEntityBodyID(AsEntity(Entity));
        if (BodyID != 0xFFFFFFFFu) { S->ActivateBody(BodyID); }
    }
}

extern "C" RUNTIME_API void LuminaSharp_Physics_DeactivateBody(uint64 World, uint32 Entity)
{
    if (Physics::IPhysicsScene* S = SceneOf(World))
    {
        const uint32 BodyID = S->GetEntityBodyID(AsEntity(Entity));
        if (BodyID != 0xFFFFFFFFu) { S->DeactivateBody(BodyID); }
    }
}

//================================================================================================
// Debug draw (World.Draw)
//================================================================================================

extern "C" RUNTIME_API void LuminaSharp_Debug_DrawLine(uint64 World, FVector3 Start, FVector3 End, FVector4 Color, float Thickness, float Duration)
{
    if (CWorld* W = AsWorld(World))
    {
        W->DrawLine(Start, End, Color, Thickness, true, Duration);
    }
}

extern "C" RUNTIME_API void LuminaSharp_Debug_DrawSphere(uint64 World, FVector3 Center, float Radius, FVector4 Color, float Thickness, float Duration)
{
    if (CWorld* W = AsWorld(World))
    {
        W->GetDebugInterface()->DrawSphere(Center, Radius, Color, TOptional<float>(Thickness), TOptional<bool>(true), TOptional<float>(Duration));
    }
}

extern "C" RUNTIME_API void LuminaSharp_Debug_DrawBox(uint64 World, FVector3 Center, FVector3 HalfExtents, FQuat Rotation, FVector4 Color, float Thickness, float Duration)
{
    if (CWorld* W = AsWorld(World))
    {
        W->GetDebugInterface()->DrawBox(Center, HalfExtents, Rotation, Color, TOptional<float>(Thickness), TOptional<bool>(true), TOptional<float>(Duration));
    }
}

extern "C" RUNTIME_API void LuminaSharp_Debug_DrawText(uint64 World, const char* Text, int32 Length, FVector4 Color)
{
    if (CWorld* W = AsWorld(World))
    {
        W->DrawDebugText(Length > 0 ? FString(Text, (size_t)Length) : FString(), Color);
    }
}

//================================================================================================
// Net (World.Net) - the role/mode queries; C# derives IsServer/IsClient/etc. from the mode.
//================================================================================================

extern "C" RUNTIME_API int32 LuminaSharp_Net_GetMode(uint64 World)
{
    CWorld* W = AsWorld(World);
    return W ? (int32)W->GetNetMode() : 0;
}

extern "C" RUNTIME_API int32 LuminaSharp_Net_GetConnectedClients(uint64 World)
{
    CWorld* W = AsWorld(World);
    return W ? W->GetConnectedClientCount() : 0;
}

//================================================================================================
// SystemContext (the per-tick context handed to a C# EntitySystem). Ctx is the live const
// FSystemContext* the native scheduler passes through the shared managed-system shim; valid only for
// the duration of the OnUpdate crossing. Each shim forwards to the matching FSystemContext method.
//================================================================================================

extern "C" RUNTIME_API float LuminaSharp_SystemContext_GetDeltaTime(const FSystemContext* Ctx)
{
    return Ctx ? (float)Ctx->GetDeltaTime() : 0.0f;
}

extern "C" RUNTIME_API double LuminaSharp_SystemContext_GetTime(const FSystemContext* Ctx)
{
    return Ctx ? Ctx->GetTime() : 0.0;
}

extern "C" RUNTIME_API uint32 LuminaSharp_SystemContext_Create(const FSystemContext* Ctx)
{
    return Ctx ? ToId(Ctx->Create()) : ToId(entt::null);
}

extern "C" RUNTIME_API void LuminaSharp_SystemContext_Destroy(const FSystemContext* Ctx, uint32 Entity)
{
    if (Ctx)
    {
        Ctx->Destroy(AsEntity(Entity));
    }
}

extern "C" RUNTIME_API void LuminaSharp_SystemContext_SetEntityLocation(const FSystemContext* Ctx, uint32 Entity, FVector3 Location)
{
    // SetEntityLocation is a non-const FSystemContext method (it mutates via the registry reference, not
    // the context object). The scheduler owns a non-const FSystemContext, so removing const here is safe.
    if (Ctx)
    {
        const_cast<FSystemContext*>(Ctx)->SetEntityLocation(AsEntity(Entity), Location);
    }
}

extern "C" RUNTIME_API void LuminaSharp_SystemContext_DrawDebugLine(const FSystemContext* Ctx, FVector3 Start, FVector3 End, FVector4 Color)
{
    if (Ctx)
    {
        Ctx->DrawDebugLine(Start, End, Color);
    }
}
