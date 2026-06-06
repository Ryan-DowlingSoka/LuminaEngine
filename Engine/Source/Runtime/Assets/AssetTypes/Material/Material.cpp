#include "pch.h"
#include "Material.h"
#include "Assets/AssetTypes/Material/MaterialInstance.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "FileSystem/FileSystem.h"
#include "Memory/MemoryTracking.h"
#include "Paths/Paths.h"
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
        LUMINA_MEMORY_SCOPE("Materials");

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

    void CMaterial::RegisterInstance(CMaterialInstance* Instance)
    {
        if (Instance == nullptr)
        {
            return;
        }
        for (CMaterialInstance* Existing : Instances)
        {
            if (Existing == Instance)
            {
                return;
            }
        }
        Instances.push_back(Instance);
    }

    void CMaterial::UnregisterInstance(CMaterialInstance* Instance)
    {
        if (Instance == nullptr)
        {
            return;
        }
        for (auto It = Instances.begin(); It != Instances.end(); ++It)
        {
            if (*It == Instance)
            {
                Instances.erase(It);
                return;
            }
        }
    }

    void CMaterial::NotifyInstancesParentChanged()
    {
        // Refresh instances whose cached uniforms are stale after a recompile or first-time load order.
        for (CMaterialInstance* Instance : Instances)
        {
            if (Instance == nullptr || Instance->Material.Get() != this)
            {
                continue;
            }

            Instance->RebuildUniformsFromOverrides();
            UpdateMaterialUniforms();
        }
    }

    void CMaterial::UpdateMaterialUniforms()
    {
        if (MaterialIndex != -1)
        {
            GRenderManager->GetMaterialManager().UpdateMaterialUniforms(&MaterialUniforms, (uint32)MaterialIndex);
        }
    }

    void CMaterial::RebuildParameterLookup()
    {
        ParameterLookup.clear();
        ParameterLookup.reserve(Parameters.size());
        for (const FMaterialParameter& Param : Parameters)
        {
            ParameterLookup[Param.ParameterName] = Param;
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

            // Depth-prepass / shadow VS only present for WPO materials; null falls back to global lib shader.
            if (!DepthPrepassVertexShaderBinaries.empty())
            {
                FShaderHeader Header;
                Header.DebugName = GetName().ToString() + "_DepthPrepassVS";
                Header.Hash      = Hash::GetHash64(DepthPrepassVertexShaderBinaries.data(), DepthPrepassVertexShaderBinaries.size() * sizeof(uint32));
                Header.Binaries  = DepthPrepassVertexShaderBinaries;
                DepthPrepassVertexShader = GRenderContext->CreateVertexShader(Header);
            }
            if (!ShadowVertexShaderBinaries.empty())
            {
                FShaderHeader Header;
                Header.DebugName = GetName().ToString() + "_ShadowVS";
                Header.Hash      = Hash::GetHash64(ShadowVertexShaderBinaries.data(), ShadowVertexShaderBinaries.size() * sizeof(uint32));
                Header.Binaries  = ShadowVertexShaderBinaries;
                ShadowVertexShader = GRenderContext->CreateVertexShader(Header);
            }

            FBindingSetDesc SetDesc;

            // FMaterialUniforms isn't serialized; replay defaults from Parameters so authored values survive load.
            for (const FMaterialParameter& Param : Parameters)
            {
                switch (Param.Type)
                {
                case EMaterialParameterType::Scalar:
                    if (Param.Index < MAX_SCALARS)
                    {
                        MaterialUniforms.Scalars[Param.Index] = Param.ScalarDefault;
                    }
                    break;
                case EMaterialParameterType::Vector:
                    if (Param.Index < MAX_VECTORS)
                    {
                        MaterialUniforms.Vectors[Param.Index] = Param.VectorDefault;
                    }
                    break;
                case EMaterialParameterType::Texture:
                    break;
                }
            }
            
            const uint32 NumTextures = (uint32)Math::Min<size_t>(Textures.size(), MAX_TEXTURES);
            for (uint32 i = 0; i < NumTextures; ++i)
            {
                FRHIImage* Image = Textures[i] ? Textures[i]->GetRHIRef() : nullptr;
                const int32 ResourceID = Image ? Image->GetResourceID() : -1;
                MaterialUniforms.Textures[i] = (ResourceID >= 0) ? (uint32)ResourceID : 0u;
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

            RebuildParameterLookup();

            if (GetMaterialIndex() == -1)
            {
                GRenderManager->GetMaterialManager().AddMaterial(this);
            }
            else
            {
                UpdateMaterialUniforms();
            }

            SetReadyForRender(true);

            NotifyInstancesParentChanged();

#if !USING(WITH_EDITOR)
            // SPIR-V blobs are dead in cooked builds; editor keeps them for recompile/save.
            auto Drop = [](TVector<uint32>& V) { V.clear(); V.shrink_to_fit(); };
            Drop(VertexShaderBinaries);
            Drop(PixelShaderBinaries);
            Drop(DepthPrepassVertexShaderBinaries);
            Drop(ShadowVertexShaderBinaries);
#endif
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
        auto It = ParameterLookup.find(Name);
        if (It != ParameterLookup.end() && It->second.Type == EMaterialParameterType::Scalar)
        {
            const FMaterialParameter& Param = It->second;
            if (Param.Index < MAX_SCALARS)
            {
                MaterialUniforms.Scalars[Param.Index] = Value;
            }
            return true;
        }

        LOG_ERROR("Failed to find material scalar parameter {}", Name);
        return false;
    }

    bool CMaterial::SetVectorValue(const FName& Name, const FVector4& Value)
    {
        auto It = ParameterLookup.find(Name);
        if (It != ParameterLookup.end() && It->second.Type == EMaterialParameterType::Vector)
        {
            const FMaterialParameter& Param = It->second;
            if (Param.Index < MAX_VECTORS)
            {
                MaterialUniforms.Vectors[Param.Index] = Value;
            }
            return true;
        }

        LOG_ERROR("Failed to find material vector parameter {}", Name);
        return false;
    }

    bool CMaterial::GetParameterValue(EMaterialParameterType Type, const FName& Name, FMaterialParameter& Param)
    {
        Param = {};
        auto It = ParameterLookup.find(Name);
        if (It != ParameterLookup.end() && It->second.Type == Type)
        {
            Param = It->second;
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
        
        if (DefaultMaterial)
        {
            DefaultMaterial->RemoveFromRoot();
            DefaultMaterial->ConditionalBeginDestroy();
            DefaultMaterial = nullptr;
        }
        
        DefaultMaterial = NewObject<CMaterial>(nullptr, "DefaultMaterial");
        DefaultMaterial->AddToRoot();
        
        FString LoadedPixelString;
        if (!VFS::ReadFile(LoadedPixelString, "/Engine/Resources/Shaders/MaterialShader/BasePixelPass.slang"))
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
        if (!VFS::ReadFile(LoadedVertexString, "/Engine/Resources/Shaders/MaterialShader/BaseVertexPass.slang"))
        {
            LOG_ERROR("Failed to find BaseVertPass.slang!");
            return;
        }

        // Default material: no-op WPO substitution.
        const char* VertexToken = "$MATERIAL_VERTEX_INPUTS";
        size_t VertexPos = LoadedVertexString.find(VertexToken);
        FString VertexReplacement = "Material.WorldPositionOffset = float3(0.0);\n";
        if (VertexPos != FString::npos)
        {
            LoadedVertexString.replace(VertexPos, strlen(VertexToken), VertexReplacement);
        }
        else
        {
            LOG_ERROR("Missing [$MATERIAL_VERTEX_INPUTS] in base vertex shader!");
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
        if (!VFS::ReadFile(LoadedPixelString, "/Engine/Resources/Shaders/MaterialShader/TerrainBasePixelPass.slang"))
        {
            LOG_ERROR("Failed to find TerrainBasePixelPass.slang!");
            return;
        }

        // Default terrain: 4-layer weighted albedo so unassigned terrain reads as distinct painted regions.
        const char* Token = "$MATERIAL_INPUTS";
        size_t PixelPos = LoadedPixelString.find(Token);

        // Slang rejects typed array initializers, so the blend is expanded into a weighted sum.
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
        if (!VFS::ReadFile(LoadedVertexString, "/Engine/Resources/Shaders/MaterialShader/TerrainBaseVertexPass.slang"))
        {
            LOG_ERROR("Failed to find TerrainBaseVertexPass.slang!");
            return;
        }

        // Default terrain: no WPO; zero-init vertex token.
        const char* VertexToken = "$MATERIAL_VERTEX_INPUTS";
        size_t VertexPos = LoadedVertexString.find(VertexToken);
        FString VertexReplacement = "Material.WorldPositionOffset = float3(0.0);\n";
        if (VertexPos != FString::npos)
        {
            LoadedVertexString.replace(VertexPos, strlen(VertexToken), VertexReplacement);
        }
        else
        {
            LOG_ERROR("Missing [$MATERIAL_VERTEX_INPUTS] in terrain base vertex shader!");
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
