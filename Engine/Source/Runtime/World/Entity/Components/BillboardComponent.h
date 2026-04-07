#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "BillboardComponent.generated.h"

namespace Lumina
{
    class CTexture;
    
    REFLECT(Component)
    struct SBillboardComponent
    {
        GENERATED_BODY()
        
        PROPERTY(Editable)
        TObjectPtr<CTexture> Texture;
        
        PROPERTY(Editable)
        float Scale = 1.0f;
        
        PROPERTY(Editable, Color)
        glm::vec4 Tint;
    };
}
