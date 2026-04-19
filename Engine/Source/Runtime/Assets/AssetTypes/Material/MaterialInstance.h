#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "MaterialInterface.h"
#include "Renderer/MaterialTypes.h"
#include "Renderer/RenderResource.h"
#include "MaterialInstance.generated.h"

namespace Lumina { class CMaterial; };

namespace Lumina
{
    REFLECT()
    class RUNTIME_API CMaterialInstance : public CMaterialInterface
    {
        GENERATED_BODY()
    public:

        CMaterialInstance();

        bool IsAsset() const override { return true; }
        
        CMaterial* GetMaterial() const override;
        bool SetScalarValue(const FName& Name, const float Value) override;
        bool SetVectorValue(const FName& Name, const glm::vec4& Value) override;
        bool GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param) override;
        FMaterialUniforms* GetMaterialUniforms() override { return &MaterialUniforms; }
        
        FRHIVertexShader* GetVertexShader() const override;
        FRHIPixelShader* GetPixelShader() const override;

        void PostLoad() override;

        /** The parent material this instance overrides parameters for. */
        PROPERTY(ReadOnly, Category = "Material")
        TObjectPtr<CMaterial> Material;


        TVector<FMaterialParameter>             Parameters;
        FMaterialUniforms                       MaterialUniforms;
        FRHIBufferRef                           UniformBuffer;
        FRHIBindingSetRef                       BindingSet;
    };
}
