#pragma once

#include "Assets/AssetTypes/Mesh/Mesh.h"
#include "SkeletalMesh.generated.h"

namespace Lumina
{
    class CSkeleton;
    
    REFLECT()
    class RUNTIME_API CSkeletalMesh : public CMesh
    {
        GENERATED_BODY()
        
    public:
        
        void Serialize(FArchive& Ar) override;
        bool IsAsset() const override { return true; }
        bool IsSkinned() const override { return true; }
        
        
        /** The skeleton asset that defines the bone hierarchy for this mesh. */
        PROPERTY(Editable, Category = "Skeleton")
        TObjectPtr<CSkeleton> Skeleton;
    };
}
