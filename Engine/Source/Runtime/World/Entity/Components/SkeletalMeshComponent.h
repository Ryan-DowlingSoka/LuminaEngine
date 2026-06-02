#pragma once
#include "meshcomponent.h"
#include "Core/Math/AABB.h"
#include "Core/Object/ObjectMacros.h"
#include "SkeletalMeshComponent.generated.h"


namespace Lumina
{
    class CMaterialInterface;
    class CSkeletalMesh;

    /** Controls whether the animation pose is evaluated when the mesh isn't being rendered. */
    REFLECT()
    enum class EAnimUpdateMode : uint8
    {
        // Evaluate the pose every frame regardless of visibility (use for gameplay-critical
        // skeletons whose pose must stay current even off-screen).
        AlwaysTickPose,

        // Skip pose evaluation while the mesh hasn't been rendered recently (the pose freezes
        // and resumes when it comes back on-screen). Default -- saves CPU on off-screen crowds.
        TickWhenRendered,
    };

    REFLECT(Component, Category = "Rendering")
    struct RUNTIME_API SSkeletalMeshComponent : SMeshComponent
    {
        GENERATED_BODY()

        CMaterialInterface* GetMaterialForSlot(size_t Slot) const;

        FUNCTION(Script)
        FAABB GetAABB() const;

        /** How the animation pose updates relative to visibility (see EAnimUpdateMode). */
        PROPERTY(Editable, Category = "Animation")
        EAnimUpdateMode VisibilityBasedAnimTick = EAnimUpdateMode::TickWhenRendered;

        // World time the render gather last kept this mesh through culling; animation systems read it
        // to gate off-screen pose evaluation. -1 until first rendered. Transient, never serialized.
        double LastRenderedTime = -1.0;
        
        /** The skeletal mesh asset to render and animate for this component. */
        PROPERTY(Editable, Category = "Mesh")
        TObjectPtr<CSkeletalMesh> SkeletalMesh;

        // Sized to the skeleton's bone count by the animation system (0 when unused). The render scene
        // uploads exactly this many matrices; FGPUInstance.BoneOffset references this instance's slice.
        TVector<FMatrix4> BoneTransforms;
    };
}
