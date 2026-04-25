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
    /**
     * Per-parameter override stored on a material instance.
     * Only parameters that diverge from the parent material need an entry here.
     */
    REFLECT()
    struct RUNTIME_API FMaterialParameterOverride
    {
        GENERATED_BODY()

        /** Name of the parameter on the parent material this override targets. */
        PROPERTY()
        FName ParameterName;

        /** Type of the parameter (Scalar, Vector, Texture). Selects which value field is used. */
        PROPERTY()
        EMaterialParameterType Type = EMaterialParameterType::Scalar;

        /** Override value for scalar parameters. */
        PROPERTY()
        float Scalar = 0.0f;

        /** Override value for vector parameters. */
        PROPERTY()
        glm::vec4 Vector = glm::vec4(0.0f);

        /** Override texture for texture parameters. */
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
        bool SetVectorValue(const FName& Name, const glm::vec4& Value) override;
        bool SetTextureValue(const FName& Name, CTexture* TextureValue);
        bool GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param) override;
        FMaterialUniforms* GetMaterialUniforms() override { return &MaterialUniforms; }

        FRHIVertexShader* GetVertexShader() const override;
        FRHIPixelShader* GetPixelShader() const override;

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

        /** Reset MaterialUniforms to the parent's defaults and re-apply every override. */
        void RebuildUniformsFromOverrides();

        /** True if a non-default override is recorded for the given parameter. */
        bool HasOverride(const FName& Name) const;

        /** Drop the override entry for the given parameter, reverting it to the parent's default. */
        void RemoveOverride(const FName& Name);

        /** The parent material this instance overrides parameters for. */
        PROPERTY(Editable, Category = "Material")
        TObjectPtr<CMaterial> Material;

        /** Per-parameter override values. Only parameters that diverge from the parent are stored here. */
        PROPERTY()
        TVector<FMaterialParameterOverride>     Overrides;

        /** Resolved parameter manifest (mirrors the parent's Parameters list at PostLoad time). */
        TVector<FMaterialParameter>             Parameters;

        /** GPU uniform block for this instance, derived from parent defaults + overrides. */
        FMaterialUniforms                       MaterialUniforms;
    };
}
