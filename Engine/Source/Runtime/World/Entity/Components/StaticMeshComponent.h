#pragma once


#include "MeshComponent.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "StaticMeshComponent.generated.h"

namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API ALIGN_FOR_FALSE_SHARING SStaticMeshComponent : SMeshComponent
    {
        GENERATED_BODY()
        
        CMaterialInterface* GetMaterialForSlot(size_t Slot) const;
        
        FUNCTION(Script)
        FAABB GetAABB() const;
        
        FUNCTION(Script)
        void SetStaticMesh(CStaticMesh* InMesh) { StaticMesh = InMesh; }
        
        FUNCTION(Script)
        CStaticMesh* GetStaticMesh() const { return StaticMesh; }
        
        PROPERTY(Editable, Category = "Rendering")
        TObjectPtr<CStaticMesh> StaticMesh;
    };
}
