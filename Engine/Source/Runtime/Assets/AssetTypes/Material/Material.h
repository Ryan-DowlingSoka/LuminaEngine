#pragma once

#include "MaterialInterface.h"
#include "Containers/Array.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "Renderer/MaterialTypes.h"
#include "Renderer/RenderResource.h"
#include "Material.generated.h"

namespace Lumina
{
    class CTexture;
    class CMaterialInstance;
}

namespace Lumina
{
    REFLECT()
    class RUNTIME_API CMaterial : public CMaterialInterface
    {
        GENERATED_BODY()

    public:

        CMaterial();

        void RegisterInstance(CMaterialInstance* Instance);
        void UnregisterInstance(CMaterialInstance* Instance);

        /** Refresh registered instance uniforms after a recompile. */
        void NotifyInstancesParentChanged();

        void Serialize(FArchive& Ar) override;
        bool IsAsset() const override { return true; }
        void PostCreateCDO() override;
        void PostLoad() override;
        void OnDestroy() override;
        
        bool SetScalarValue(const FName& Name, const float Value) override;
        bool SetVectorValue(const FName& Name, const FVector4& Value) override;
        bool GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param) override;
        FMaterialUniforms* GetMaterialUniforms() override { return &MaterialUniforms; }
        
        CMaterial* GetMaterial() const override;
        const FShaderEntry* GetVertexShader() const override;
        const FShaderEntry* GetPixelShader() const override;

        // Per-material depth-prepass / shadow VS only populated for WPO materials; null falls back to global lib.
        const FShaderEntry* GetDepthPrepassVertexShader() const { return DepthPrepassVertexShader; }
        const FShaderEntry* GetShadowVertexShader() const { return ShadowVertexShader; }
        bool UsesWorldPositionOffset() const { return bUsesWorldPositionOffset; }

        static CMaterial* GetDefaultMaterial();
        static CMaterial* GetDefaultTerrainMaterial();

        static void CreateDefaultMaterial();
        static void CreateDefaultTerrainMaterial();

        EMaterialType GetMaterialType() const override { return MaterialType; }
        bool DoesCastShadows() const override { return bCastShadows; }
        bool IsTwoSided() const override { return bTwoSided; }
        bool IsTranslucent() override { return BlendMode == EBlendMode::Translucent; }
        bool IsMasked() override { return BlendMode == EBlendMode::Masked; }
        bool IsAdditive() override { return BlendMode == EBlendMode::Additive; }
        bool IsOpaque() override { return BlendMode == EBlendMode::Opaque; }
        bool IsUnlit() override { return ShadingModel == EMaterialShadingModel::Unlit; }
        bool DisableDepthTest() override { return bDisableDepthTest; }
        EBlendMode GetBlendMode() override { return BlendMode; }
        EMaterialShadingModel GetShadingModel() override { return ShadingModel; }
        float GetOpacityMaskClipValue() override { return OpacityMaskClipValue; }

        PROPERTY(Editable)
        EMaterialType MaterialType;

        PROPERTY(Editable)
        EBlendMode BlendMode = EBlendMode::Opaque;

        PROPERTY(Editable)
        EMaterialShadingModel ShadingModel = EMaterialShadingModel::Lit;

        PROPERTY(Editable)
        bool bCastShadows = true;

        PROPERTY(Editable)
        bool bTwoSided = false;

        PROPERTY(Editable)
        bool bDisableDepthTest = false;

        /** Masked blend threshold; pixels below are discarded. */
        PROPERTY(Editable)
        float OpacityMaskClipValue = 0.333f;

        PROPERTY()
        TVector<TObjectPtr<CTexture>>           Textures;

        PROPERTY()
        TVector<uint32>                         PixelShaderBinaries;

        PROPERTY()
        TVector<uint32>                         VertexShaderBinaries;

        /** Empty when bUsesWorldPositionOffset is false. */
        PROPERTY()
        TVector<uint32>                         DepthPrepassVertexShaderBinaries;

        /** Empty when bUsesWorldPositionOffset is false. */
        PROPERTY()
        TVector<uint32>                         ShadowVertexShaderBinaries;

        /** True when the graph's WPO pin is connected; gates per-material depth/shadow shader selection. */
        PROPERTY()
        bool                                    bUsesWorldPositionOffset = false;

        PROPERTY()
        TVector<FMaterialParameter>             Parameters;

        FMaterialUniforms                       MaterialUniforms;

        // Library entries keyed by asset GUID; recompiles refresh them in place.
        const FShaderEntry*                     VertexShader = nullptr;
        const FShaderEntry*                     PixelShader = nullptr;
        const FShaderEntry*                     DepthPrepassVertexShader = nullptr;
        const FShaderEntry*                     ShadowVertexShader = nullptr;
        
    protected:
        
        void UpdateMaterialUniforms() override;

    private:

        void RebuildParameterLookup();

        /** Instance back-references; instances unregister in OnDestroy so raw pointers are safe. */
        TVector<CMaterialInstance*>             Instances;

        THashMap<FName, FMaterialParameter>     ParameterLookup;
    };
    
}
