#pragma once

#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "World/Scene/RenderScene/MeshBatch.h"
#include "Renderer/CustomPrimitiveData.h"
#include "MeshComponent.generated.h"

namespace Lumina
{
    class CMaterialInterface;
    
    REFLECT()
    struct RUNTIME_API SMeshComponent
    {
        GENERATED_BODY()
        
        PROPERTY(Editable, Category = "Rendering")
        TVector<TObjectPtr<CMaterialInterface>> MaterialOverrides;
        
        PROPERTY(Editable, Category = "Rendering")
        SCustomPrimitiveData CustomPrimitiveData{};

        PROPERTY(Editable, Category = "Culling")
        float MaxDrawDistance = 0.0f;

        PROPERTY(Editable, Category = "Culling")
        float BoundsScale = 1.0f;

        PROPERTY(Editable, Category = "Culling")
        bool bUseAsOccluder = true;

        PROPERTY(Editable, Category = "Culling")
        bool bIgnoreOcclusionCulling = false;

        PROPERTY(Editable, Category = "Shadows")
        bool bCastShadow = true;

        PROPERTY(Editable, Category = "Shadows")
        bool bReceiveShadow = true;
        
    };
}
