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

        // World time (seconds) the render scene last kept this mesh through culling. Set by the
        // renderer's gather; read by the animation systems to gate off-screen pose evaluation.
        // -1 until first rendered. Transient render bookkeeping, never serialized.
        double LastRenderedTime = -1.0;
        
        /** The skeletal mesh asset to render and animate for this component. */
        PROPERTY(Editable, Category = "Mesh")
        TObjectPtr<CSkeletalMesh> SkeletalMesh;

        /** When true, this mesh writes to the shadow map. */
        PROPERTY(Editable, Category = "Shadow")
        bool bCastShadow = true;

        /** When true, this mesh receives shadowing from shadow-casting lights. */
        PROPERTY(Editable, Category = "Shadow")
        bool bReceiveShadow = true;

        // Sized to the skeleton's bone count by the animation system (or to 0 when unused).
        // The render scene uploads exactly this many matrices into the shared bone buffer
        // and FGPUInstance.BoneOffset references the start of this instance's slice.
        TVector<glm::mat4> BoneTransforms;
    };
}
