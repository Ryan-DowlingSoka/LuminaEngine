#include "pch.h"
#include "MaterialInstance.h"
#include "Material.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Engine/Engine.h"
#include "Renderer/RenderManager.h"


namespace Lumina
{
    CMaterialInstance::CMaterialInstance()
    {
        Memory::Memzero(&MaterialUniforms, sizeof(FMaterialUniforms));
    }

    CMaterial* CMaterialInstance::GetMaterial() const
    {
        return Material.Get();
    }

    static void ApplyOverride(const FMaterialParameterOverride& Override, const TVector<FMaterialParameter>& Params, FMaterialUniforms& Uniforms)
    {
        for (const FMaterialParameter& Param : Params)
        {
            if (Param.ParameterName != Override.ParameterName || Param.Type != Override.Type)
            {
                continue;
            }

            switch (Override.Type)
            {
            case EMaterialParameterType::Scalar:
                if (Param.Index < MAX_SCALARS)
                {
                    Uniforms.Scalars[Param.Index] = Override.Scalar;
                }
                break;
            case EMaterialParameterType::Vector:
                if (Param.Index < MAX_VECTORS)
                {
                    Uniforms.Vectors[Param.Index] = Override.Vector;
                }
                break;
            case EMaterialParameterType::Texture:
                if (Param.Index < MAX_TEXTURES && Override.Texture && Override.Texture->TextureResource && Override.Texture->TextureResource->RHIImage.IsValid())
                {
                    Uniforms.Textures[Param.Index] = Override.Texture->GetRHIRef()->GetResourceID();
                }
                break;
            }
            return;
        }
    }

    void CMaterialInstance::RebuildUniformsFromOverrides()
    {
        if (!Material)
        {
            return;
        }

        Parameters = Material->Parameters;
        MaterialUniforms = Material->MaterialUniforms;

        // Drop overrides whose parent parameter is gone/retyped, otherwise dead entries persist forever.
        Overrides.erase(eastl::remove_if(Overrides.begin(), Overrides.end(),
            [this](const FMaterialParameterOverride& O)
            {
                FMaterialParameter Probe;
                return !Material->GetParameterValue(O.Type, O.ParameterName, Probe);
            }), Overrides.end());

        for (const FMaterialParameterOverride& Override : Overrides)
        {
            ApplyOverride(Override, Parameters, MaterialUniforms);
        }
    }

    static FMaterialParameterOverride& FindOrAddOverride(TVector<FMaterialParameterOverride>& Overrides, const FName& Name, EMaterialParameterType Type)
    {
        for (FMaterialParameterOverride& O : Overrides)
        {
            if (O.ParameterName == Name && O.Type == Type)
            {
                return O;
            }
        }

        FMaterialParameterOverride New;
        New.ParameterName = Name;
        New.Type = Type;
        Overrides.push_back(New);
        return Overrides.back();
    }

    bool CMaterialInstance::SetScalarValue(const FName& Name, const float Value)
    {
        if (!Material)
        {
            return false;
        }

        FMaterialParameter Param;
        if (!Material->GetParameterValue(EMaterialParameterType::Scalar, Name, Param))
        {
            LOG_ERROR("Failed to find parent scalar parameter '{}'", Name);
            return false;
        }

        if (Param.Index < MAX_SCALARS)
        {
            MaterialUniforms.Scalars[Param.Index] = Value;
        }

        FindOrAddOverride(Overrides, Name, EMaterialParameterType::Scalar).Scalar = Value;

        UpdateMaterialUniforms();
        
        return true;
    }

    bool CMaterialInstance::SetVectorValue(const FName& Name, const FVector4& Value)
    {
        if (!Material)
        {
            return false;
        }

        FMaterialParameter Param;
        if (!Material->GetParameterValue(EMaterialParameterType::Vector, Name, Param))
        {
            LOG_ERROR("Failed to find parent vector parameter '{}'", Name);
            return false;
        }

        if (Param.Index < MAX_VECTORS)
        {
            MaterialUniforms.Vectors[Param.Index] = Value;
        }

        FindOrAddOverride(Overrides, Name, EMaterialParameterType::Vector).Vector = Value;

        UpdateMaterialUniforms();

        return true;
    }

    bool CMaterialInstance::SetTextureValue(const FName& Name, CTexture* TextureValue)
    {
        if (!Material)
        {
            return false;
        }

        FMaterialParameter Param;
        if (!Material->GetParameterValue(EMaterialParameterType::Texture, Name, Param))
        {
            LOG_ERROR("Failed to find parent texture parameter '{}'", Name);
            return false;
        }

        if (Param.Index < MAX_TEXTURES)
        {
            if (TextureValue && TextureValue->TextureResource && TextureValue->TextureResource->RHIImage.IsValid())
            {
                MaterialUniforms.Textures[Param.Index] = TextureValue->GetRHIRef()->GetResourceID();
            }
            else
            {
                // Fall back to parent default texture.
                MaterialUniforms.Textures[Param.Index] = Material->MaterialUniforms.Textures[Param.Index];
            }
        }

        FindOrAddOverride(Overrides, Name, EMaterialParameterType::Texture).Texture = TextureValue;
        
        return true;
    }

    bool CMaterialInstance::GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param)
    {
        Param = {};
        auto* Itr = eastl::find_if(Parameters.begin(), Parameters.end(), [Type, Name](const FMaterialParameter& Param)
        {
           return Param.ParameterName == Name && Param.Type == Type;
        });

        if (Itr != Parameters.end())
        {
            Param = *Itr;
            return true;
        }

        return false;
    }

    bool CMaterialInstance::HasOverride(const FName& Name) const
    {
        for (const FMaterialParameterOverride& O : Overrides)
        {
            if (O.ParameterName == Name)
            {
                return true;
            }
        }
        return false;
    }

    void CMaterialInstance::RemoveOverride(const FName& Name)
    {
        Overrides.erase(eastl::remove_if(Overrides.begin(), Overrides.end(), [Name](const FMaterialParameterOverride& O)
        {
            return O.ParameterName == Name;
        }), Overrides.end());

        RebuildUniformsFromOverrides();

        UpdateMaterialUniforms();
    }

    void CMaterialInstance::UpdateMaterialUniforms()
    {
        if (MaterialIndex != -1)
        {
            GRenderManager->GetMaterialManager().UpdateMaterialUniforms(&MaterialUniforms, (uint32)MaterialIndex);
        }
    }

    FRHIVertexShader* CMaterialInstance::GetVertexShader() const
    {
        return Material ? Material->GetVertexShader() : nullptr;
    }

    FRHIPixelShader* CMaterialInstance::GetPixelShader() const
    {
        return Material ? Material->GetPixelShader() : nullptr;
    }

    EMaterialType CMaterialInstance::GetMaterialType() const
    {
        return Material ? Material->GetMaterialType() : EMaterialType::None;
    }

    bool CMaterialInstance::DoesCastShadows() const
    {
        return Material ? Material->DoesCastShadows() : false;
    }

    bool CMaterialInstance::IsTwoSided() const
    {
        return Material ? Material->IsTwoSided() : false;
    }

    bool CMaterialInstance::IsTranslucent()
    {
        return Material ? Material->IsTranslucent() : false;
    }

    bool CMaterialInstance::IsMasked()
    {
        return Material ? Material->IsMasked() : false;
    }

    bool CMaterialInstance::IsAdditive()
    {
        return Material ? Material->IsAdditive() : false;
    }

    bool CMaterialInstance::IsOpaque()
    {
        return Material ? Material->IsOpaque() : true;
    }

    bool CMaterialInstance::IsUnlit()
    {
        return Material ? Material->IsUnlit() : false;
    }

    bool CMaterialInstance::DisableDepthTest()
    {
        return Material ? Material->DisableDepthTest() : false;
    }

    EBlendMode CMaterialInstance::GetBlendMode()
    {
        return Material ? Material->GetBlendMode() : EBlendMode::Opaque;
    }

    EMaterialShadingModel CMaterialInstance::GetShadingModel()
    {
        return Material ? Material->GetShadingModel() : EMaterialShadingModel::Lit;
    }

    float CMaterialInstance::GetOpacityMaskClipValue()
    {
        return Material ? Material->GetOpacityMaskClipValue() : 0.333f;
    }

    void CMaterialInstance::PostLoad()
    {
        if (!Material)
        {
            return;
        }

        // Register before parent's PostLoad so NotifyInstancesParentChanged refreshes our cached params.
        Material->RegisterInstance(this);

        if (!Material->IsReadyForRender())
        {
            Material->PostLoad();
        }
        else
        {
            RebuildUniformsFromOverrides();
        }

        if (GetMaterialIndex() == -1)
        {
            GRenderManager->GetMaterialManager().AddMaterial(this);
        }
        else
        {
            UpdateMaterialUniforms();
        }

        SetReadyForRender(true);
    }

    void CMaterialInstance::OnDestroy()
    {
        CMaterialInterface::OnDestroy();

        if (Material)
        {
            Material->UnregisterInstance(this);
        }

        if (GetMaterialIndex() != -1)
        {
            GRenderManager->GetMaterialManager().RemoveMaterial(this);
        }
    }
}
