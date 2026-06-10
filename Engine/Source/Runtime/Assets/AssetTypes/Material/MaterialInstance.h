#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "MaterialInterface.h"
#include "Renderer/MaterialTypes.h"
#include "MaterialInstance.generated.h"

namespace Lumina
{
    class CMaterial;
    class CTexture;
}

namespace Lumina
{
    /** Per-parameter override on a material instance; only divergent parameters are stored. */
    REFLECT()
    struct RUNTIME_API FMaterialParameterOverride
    {
        GENERATED_BODY()

        PROPERTY()
        FName ParameterName;

        PROPERTY()
        EMaterialParameterType Type = EMaterialParameterType::Scalar;

        PROPERTY()
        float Scalar = 0.0f;

        PROPERTY()
        FVector4 Vector = FVector4(0.0f);

        PROPERTY()
        TObjectPtr<CTexture> Texture;
    };


    REFLECT()
    class RUNTIME_API CMaterialInstance : public CMaterialInterface
    {
        GENERATED_BODY()
    public:

        CMaterialInstance();

        bool IsAsset() const override { return true; }

        CMaterial* GetMaterial() const override;
        bool SetScalarValue(const FName& Name, const float Value) override;
        bool SetVectorValue(const FName& Name, const FVector4& Value) override;
        bool SetTextureValue(const FName& Name, CTexture* TextureValue);
        bool GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param) override;
        FMaterialUniforms* GetMaterialUniforms() override { return &MaterialUniforms; }
        const TVector<FMaterialParameter>& GetMaterialParams() const { return Parameters; }

        const FShaderEntry* GetVertexShader() const override;
        const FShaderEntry* GetPixelShader() const override;

        EMaterialType GetMaterialType() const override;
        bool DoesCastShadows() const override;
        bool IsTwoSided() const override;
        bool IsTranslucent() override;
        bool IsMasked() override;
        bool IsAdditive() override;
        bool IsOpaque() override;
        bool IsUnlit() override;
        bool DisableDepthTest() override;
        EBlendMode GetBlendMode() override;
        EMaterialShadingModel GetShadingModel() override;
        float GetOpacityMaskClipValue() override;
        

        void PostLoad() override;
        void OnDestroy() override;

        /** Reset uniforms to parent defaults and re-apply every override. */
        void RebuildUniformsFromOverrides();

        bool HasOverride(const FName& Name) const;
        void RemoveOverride(const FName& Name);

        PROPERTY(ReadOnly, Category = "Material")
        TObjectPtr<CMaterial> Material;

        PROPERTY()
        TVector<FMaterialParameterOverride>     Overrides;
        
    protected:
        
        void UpdateMaterialUniforms() override;
        
    private:
        
        TVector<FMaterialParameter>             Parameters;
        FMaterialUniforms                       MaterialUniforms;
    };
}
