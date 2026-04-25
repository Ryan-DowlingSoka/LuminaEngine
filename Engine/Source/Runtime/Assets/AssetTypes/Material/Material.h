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

        /** Register a live instance whose parent is this material. Called by CMaterialInstance::PostLoad. */
        void RegisterInstance(CMaterialInstance* Instance);

        /** Unregister a live instance. Called by CMaterialInstance::OnDestroy or when the parent reference changes. */
        void UnregisterInstance(CMaterialInstance* Instance);

        /** Refresh all registered instances after a recompile (calls RebuildUniformsFromOverrides + UpdateMaterialUniforms). */
        void NotifyInstancesParentChanged();

        void Serialize(FArchive& Ar) override;
        bool IsAsset() const override { return true; }
        void PostCreateCDO() override;
        void PostLoad() override;
        void OnDestroy() override;
        
        bool SetScalarValue(const FName& Name, const float Value) override;
        bool SetVectorValue(const FName& Name, const glm::vec4& Value) override;
        bool GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param) override;
        FMaterialUniforms* GetMaterialUniforms() override { return &MaterialUniforms; }
        
        CMaterial* GetMaterial() const override;
        FRHIVertexShader* GetVertexShader() const override;
        FRHIPixelShader* GetPixelShader() const override;
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

        /** Domain of the material (Surface, PostProcess, etc.). */
        PROPERTY(Editable)
        EMaterialType MaterialType;

        /** Controls how the material composites with the scene (Opaque, Masked, Translucent, Additive). */
        PROPERTY(Editable)
        EBlendMode BlendMode = EBlendMode::Opaque;

        /** Lighting model used during shading (Lit, Unlit, etc.). */
        PROPERTY(Editable)
        EMaterialShadingModel ShadingModel = EMaterialShadingModel::Lit;

        /** When true, objects using this material write to the shadow map. */
        PROPERTY(Editable)
        bool bCastShadows = true;

        /** When true, back faces are rendered as well as front faces. */
        PROPERTY(Editable)
        bool bTwoSided = false;

        /** When true, the depth test is skipped for surfaces using this material. */
        PROPERTY(Editable)
        bool bDisableDepthTest = false;

        /** Opacity threshold for Masked blend mode, pixels below this value are discarded. */
        PROPERTY(Editable)
        float OpacityMaskClipValue = 0.333f;

        /** Texture slots bound to this material, indexed by the Parameters list. */
        PROPERTY()
        TVector<TObjectPtr<CTexture>>           Textures;

        /** Compiled SPIR-V bytecode for the pixel shader. */
        PROPERTY()
        TVector<uint32>                         PixelShaderBinaries;

        /** Compiled SPIR-V bytecode for the vertex shader. */
        PROPERTY()
        TVector<uint32>                         VertexShaderBinaries;

        /** Declared material parameters (scalars, vectors, textures) with their slot indices. */
        PROPERTY()
        TVector<FMaterialParameter>             Parameters;

        FMaterialUniforms                       MaterialUniforms;

        FRHIVertexShaderRef                     VertexShader;
        FRHIPixelShaderRef                      PixelShader;

    private:

        /** Rebuilds ParameterLookup from Parameters. Called after PostLoad / recompile. */
        void RebuildParameterLookup();

        /** Live instance back-reference list. Built at runtime by CMaterialInstance::PostLoad / OnDestroy.
         *  Raw pointers are safe because the instance pre-emptively unregisters in its OnDestroy. */
        TVector<CMaterialInstance*>             Instances;

        /** O(1) parameter lookup by name. Rebuilt in PostLoad / on recompile. */
        THashMap<FName, FMaterialParameter>     ParameterLookup;
    };
    
}
