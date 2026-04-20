#include "pch.h"
#include "Material.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/ShaderCompiler.h"
#include "Types/Byte.h"

namespace Lumina
{
    static CMaterial* DefaultMaterial = nullptr;
    static CMaterial* DefaultTerrainMaterial = nullptr;

    CMaterial::CMaterial()
    {
        MaterialType = EMaterialType::PBR;
        Memory::Memzero(&MaterialUniforms, sizeof(FMaterialUniforms));
    }

    void CMaterial::Serialize(FArchive& Ar)
    {
        CMaterialInterface::Serialize(Ar);
    }

    void CMaterial::PostCreateCDO()
    {
        if (DefaultMaterial == nullptr)
        {
            CreateDefaultMaterial();
        }
        if (DefaultTerrainMaterial == nullptr)
        {
            CreateDefaultTerrainMaterial();
        }
    }

    void CMaterial::PostLoad()
    {
        if (!PixelShaderBinaries.empty() && !VertexShaderBinaries.empty())
        {
            FShaderHeader VertexHeader;
            VertexHeader.DebugName      = GetName().ToString() + "_VertexShader";
            VertexHeader.Hash           = Hash::GetHash64(VertexShaderBinaries.data(), VertexShaderBinaries.size() * sizeof(uint32));
            VertexHeader.Binaries       = VertexShaderBinaries;
            VertexShader                = GRenderContext->CreateVertexShader(VertexHeader);
            
            FShaderHeader PixelHeader;
            PixelHeader.DebugName       = GetName().ToString() + "_PixelShader";
            PixelHeader.Hash            = Hash::GetHash64(PixelShaderBinaries.data(), PixelShaderBinaries.size() * sizeof(uint32));
            PixelHeader.Binaries        = PixelShaderBinaries;
            PixelShader                 = GRenderContext->CreatePixelShader(PixelHeader);

            FBindingSetDesc SetDesc;

            uint32 Index = 0;
            for (CTexture* Binding : Textures)
            {
                if (Binding != nullptr)
                {
                    MaterialUniforms.Textures[Index] = Binding->GetRHIRef()->GetTextureCacheIndex();
                    Index++;   
                }
            }
            
            EMaterialGPUFlags GPUFlags = EMaterialGPUFlags::None;
            if (BlendMode == EBlendMode::Masked)
            {
                GPUFlags |= EMaterialGPUFlags::Masked;
            }
            if (BlendMode == EBlendMode::Translucent)
            {
                GPUFlags |= EMaterialGPUFlags::Translucent;
            }
            if (BlendMode == EBlendMode::Additive)
            {
                GPUFlags |= EMaterialGPUFlags::Additive;
            }
            if (ShadingModel == EMaterialShadingModel::Unlit)
            {
                GPUFlags |= EMaterialGPUFlags::Unlit;
            }
            MaterialUniforms.Flags = (uint32)GPUFlags;
            MaterialUniforms.OpacityClipValue = OpacityMaskClipValue;

            if (GetMaterialIndex() == -1)
            {
                GRenderManager->GetMaterialManager().AddMaterial(this);
            }
            else
            {
                GRenderManager->GetMaterialManager().UpdateMaterialUniforms(this);
            }

            SetReadyForRender(true);
        }
    }

    void CMaterial::OnDestroy()
    {
        CMaterialInterface::OnDestroy();
        
        if (GetMaterialIndex() != -1)
        {
            GRenderManager->GetMaterialManager().RemoveMaterial(this);
        }
    }

    bool CMaterial::SetScalarValue(const FName& Name, const float Value)
    {
        auto* Itr = eastl::find_if(Parameters.begin(), Parameters.end(), [Name](const FMaterialParameter& Param)
        {
           return Param.ParameterName == Name && Param.Type == EMaterialParameterType::Scalar; 
        });

        if (Itr != Parameters.end())
        {
            const FMaterialParameter& Param = *Itr;
            
            MaterialUniforms.Scalars[Param.Index] = Value;
            return true;
        }
        else
        {
            LOG_ERROR("Failed to find material scalar parameter {}", Name);
        }

        return false;
    }

    bool CMaterial::SetVectorValue(const FName& Name, const glm::vec4& Value)
    {
        auto* Itr = eastl::find_if(Parameters.begin(), Parameters.end(), [Name](const FMaterialParameter& Param)
        {
           return Param.ParameterName == Name && Param.Type == EMaterialParameterType::Vector; 
        });

        if (Itr != Parameters.end())
        {
            const FMaterialParameter& Param = *Itr;
            MaterialUniforms.Vectors[Param.Index] = Value;
            return true;
        }
        else
        {
            LOG_ERROR("Failed to find material vector parameter {}", Name);
        }

        return false;
    }

    bool CMaterial::GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param)
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

    CMaterial* CMaterial::GetMaterial() const
    {
        return const_cast<CMaterial*>(this);
    }
    
    FRHIVertexShader* CMaterial::GetVertexShader() const
    {
        return VertexShader;
    }

    FRHIPixelShader* CMaterial::GetPixelShader() const
    {
        return PixelShader;
    }

    CMaterial* CMaterial::GetDefaultMaterial()
    {
        return DefaultMaterial;
    }

    CMaterial* CMaterial::GetDefaultTerrainMaterial()
    {
        return DefaultTerrainMaterial;
    }

    void CMaterial::CreateDefaultMaterial()
    {
        IShaderCompiler* ShaderCompiler = GRenderContext->GetShaderCompiler();

        ShaderCompiler->Flush();

        FString PixelPath = Paths::GetEngineResourceDirectory() + "/Shaders/MaterialShader/BasePixelPass.slang";
        FString VertexPath = Paths::GetEngineResourceDirectory() + "/Shaders/MaterialShader/BaseVertexPass.slang";

        if (DefaultMaterial)
        {
            DefaultMaterial->RemoveFromRoot();
            DefaultMaterial->ConditionalBeginDestroy();
            DefaultMaterial = nullptr;
        }
        
        DefaultMaterial = NewObject<CMaterial>(nullptr, "DefaultMaterial");
        DefaultMaterial->AddToRoot();
        
        FString LoadedPixelString;
        if (!FileHelper::LoadFileIntoString(LoadedPixelString, PixelPath))
        {
            LOG_ERROR("Failed to find BasePixelPass.slang!");
            return;
        }

        const char* Token = "$MATERIAL_INPUTS";
        size_t PixelPos = LoadedPixelString.find(Token);

        FString PixelReplacement;
        
        PixelReplacement += "\tFMaterialPixelInputs Material;\n";
        PixelReplacement += "\tMaterial.Diffuse               = float3(1.0);\n";
        PixelReplacement += "\tMaterial.Metallic              = 0.0;\n";
        PixelReplacement += "\tMaterial.Roughness             = 1.0;\n";
        PixelReplacement += "\tMaterial.Specular              = 0.5;\n";
        PixelReplacement += "\tMaterial.Emissive              = float3(0.0);\n";
        PixelReplacement += "\tMaterial.AmbientOcclusion      = 1.0;\n";
        PixelReplacement += "\tMaterial.Normal                = float3(0.0, 0.0, 1.0);\n";
        PixelReplacement += "\tMaterial.Opacity               = 1.0;\n";
        
        if (PixelPos != FString::npos)
        {
            LoadedPixelString.replace(PixelPos, strlen(Token), PixelReplacement);
        }
        else
        {
            LOG_ERROR("Missing [$MATERIAL_INPUTS] in base shader!");
        }
        
        FString LoadedVertexString;
        if (!FileHelper::LoadFileIntoString(LoadedVertexString, VertexPath))
        {
            LOG_ERROR("Failed to find BaseVertPass.slang!");
            return;
        }
        
        ShaderCompiler->CompilerShaderRaw(Move(LoadedPixelString), {}, [](const FShaderHeader& Header) mutable 
        {
            DefaultMaterial->PixelShader = GRenderContext->CreatePixelShader(Header);
            DefaultMaterial->PixelShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
            GRenderContext->OnShaderCompiled(DefaultMaterial->PixelShader, false, true);
        });
        
        ShaderCompiler->CompilerShaderRaw(Move(LoadedVertexString), {}, [](const FShaderHeader& Header) mutable 
        {
            DefaultMaterial->VertexShader = GRenderContext->CreateVertexShader(Header);
            DefaultMaterial->VertexShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
            GRenderContext->OnShaderCompiled(DefaultMaterial->PixelShader, false, true);
        });
        
        ShaderCompiler->Flush();

        DefaultMaterial->PostLoad();
    }

    void CMaterial::CreateDefaultTerrainMaterial()
    {
        IShaderCompiler* ShaderCompiler = GRenderContext->GetShaderCompiler();

        ShaderCompiler->Flush();

        FString PixelPath  = Paths::GetEngineResourceDirectory() + "/Shaders/MaterialShader/TerrainBasePixelPass.slang";
        FString VertexPath = Paths::GetEngineResourceDirectory() + "/Shaders/MaterialShader/TerrainBaseVertexPass.slang";

        if (DefaultTerrainMaterial)
        {
            DefaultTerrainMaterial->RemoveFromRoot();
            DefaultTerrainMaterial->ConditionalBeginDestroy();
            DefaultTerrainMaterial = nullptr;
        }

        DefaultTerrainMaterial = NewObject<CMaterial>(nullptr, "DefaultTerrainMaterial");
        DefaultTerrainMaterial->AddToRoot();
        DefaultTerrainMaterial->MaterialType = EMaterialType::Terrain;

        FString LoadedPixelString;
        if (!FileHelper::LoadFileIntoString(LoadedPixelString, PixelPath))
        {
            LOG_ERROR("Failed to find TerrainBasePixelPass.slang!");
            return;
        }

        // Default terrain material: 4-layer weighted albedo with hardcoded defaults,
        // so an unassigned terrain still reads as distinct painted regions rather
        // than a single flat color. A user-authored CMaterial replaces this via its
        // own graph-compiled $MATERIAL_INPUTS.
        const char* Token = "$MATERIAL_INPUTS";
        size_t PixelPos = LoadedPixelString.find(Token);

        // Layer palette: dry earth / grass / rock / sand. Slang rejects typed array
        // initializers here, so the blend is expanded into a straight weighted sum.
        FString PixelReplacement;
        PixelReplacement += "\tfloat4 _LayerW = GetTerrainLayerWeights4(HeightUV);\n";
        PixelReplacement += "\tfloat3 _TerrainAlbedo = float3(0.45, 0.40, 0.30) * _LayerW.x\n";
        PixelReplacement += "\t                     + float3(0.25, 0.45, 0.15) * _LayerW.y\n";
        PixelReplacement += "\t                     + float3(0.55, 0.55, 0.55) * _LayerW.z\n";
        PixelReplacement += "\t                     + float3(0.85, 0.80, 0.60) * _LayerW.w;\n";
        PixelReplacement += "\tFMaterialPixelInputs Material;\n";
        PixelReplacement += "\tMaterial.Diffuse               = _TerrainAlbedo;\n";
        PixelReplacement += "\tMaterial.Metallic              = 0.0;\n";
        PixelReplacement += "\tMaterial.Roughness             = 0.9;\n";
        PixelReplacement += "\tMaterial.Specular              = 0.5;\n";
        PixelReplacement += "\tMaterial.Emissive              = float3(0.0);\n";
        PixelReplacement += "\tMaterial.AmbientOcclusion      = 1.0;\n";
        PixelReplacement += "\tMaterial.Normal                = float3(0.0, 0.0, 1.0);\n";
        PixelReplacement += "\tMaterial.Opacity               = 1.0;\n";

        if (PixelPos != FString::npos)
        {
            LoadedPixelString.replace(PixelPos, strlen(Token), PixelReplacement);
        }
        else
        {
            LOG_ERROR("Missing [$MATERIAL_INPUTS] in terrain base shader!");
        }

        FString LoadedVertexString;
        if (!FileHelper::LoadFileIntoString(LoadedVertexString, VertexPath))
        {
            LOG_ERROR("Failed to find TerrainBaseVertexPass.slang!");
            return;
        }

        ShaderCompiler->CompilerShaderRaw(Move(LoadedPixelString), {}, [](const FShaderHeader& Header) mutable
        {
            DefaultTerrainMaterial->PixelShader = GRenderContext->CreatePixelShader(Header);
            DefaultTerrainMaterial->PixelShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
            GRenderContext->OnShaderCompiled(DefaultTerrainMaterial->PixelShader, false, true);
        });

        ShaderCompiler->CompilerShaderRaw(Move(LoadedVertexString), {}, [](const FShaderHeader& Header) mutable
        {
            DefaultTerrainMaterial->VertexShader = GRenderContext->CreateVertexShader(Header);
            DefaultTerrainMaterial->VertexShaderBinaries.assign(Header.Binaries.begin(), Header.Binaries.end());
            GRenderContext->OnShaderCompiled(DefaultTerrainMaterial->VertexShader, false, true);
        });

        ShaderCompiler->Flush();

        DefaultTerrainMaterial->PostLoad();
    }
}
