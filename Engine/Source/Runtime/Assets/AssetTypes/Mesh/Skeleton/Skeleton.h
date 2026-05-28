#pragma once

#include "Core/Object/Object.h"
#include "Renderer/MeshData.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Memory/SmartPtr.h"
#include "Skeleton.generated.h"

namespace Lumina
{
    class CSkeletalMesh;
    
    
    REFLECT()
    class RUNTIME_API CSkeleton : public CObject
    {
        friend class CMeshFactory;
        
        GENERATED_BODY()
        
    public:
        void Serialize(FArchive& Ar) override;

        bool IsAsset() const override { return true; }

        FSkeletonResource* GetSkeletonResource() const { return SkeletonResource.get(); }
        
        void ComputeBindPoseSkinningMatrices(TVector<FMatrix4>& OutMatrices) const;
        
        PROPERTY(Editable, Category = "Preview")
        TObjectPtr<CSkeletalMesh> PreviewMesh;
        
    private:
        
        TUniquePtr<FSkeletonResource> SkeletonResource;
    };
}
