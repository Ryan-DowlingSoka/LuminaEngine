#pragma once
#include "meshcomponent.h"
#include "Core/Math/AABB.h"
#include "Core/Object/ObjectMacros.h"
#include "SkeletalMeshComponent.generated.h"


namespace Lumina
{
    class CMaterialInterface;
    class CSkeletalMesh;
    
    
    REFLECT(Component)
    struct RUNTIME_API SSkeletalMeshComponent : SMeshComponent
    {
        GENERATED_BODY()
        
        CMaterialInterface* GetMaterialForSlot(size_t Slot) const;
        
        FUNCTION(Script)
        FAABB GetAABB() const;
        
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
