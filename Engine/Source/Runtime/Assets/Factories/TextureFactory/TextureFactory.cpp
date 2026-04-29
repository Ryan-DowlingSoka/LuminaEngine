#include "pch.h"
#include "TextureFactory.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Object/Package/Package.h"
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "encoder/basisu_comp.h"
#include "encoder/basisu_enc.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RendererUtils.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RHIGlobals.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina
{
    CObject* CTextureFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        return NewObject<CTexture>(Package, Name);
    }

    // Encodes pre-loaded raw RGBA8 pixels via Basis Universal and writes the
    // compressed image into Texture->TextureResource. Shared by initial
    // import and Recook so the format-selection logic lives in exactly one
    // place. Returns false on any compressor / transcoder error; callers
    // are responsible for cleaning up the texture on failure.
    static bool CookTexturePixels(CTexture* Texture, const TVector<uint8>& Pixels, glm::uvec2 Dimensions, ETextureColorSpace ColorSpace)
    {
        const bool bIsSRGB     = (ColorSpace == ETextureColorSpace::SRGB);
        const bool bIsNormalMap = (ColorSpace == ETextureColorSpace::NormalMap);

        basisu::job_pool JobPool(Threading::GetNumThreads() - 1);

        basisu::basis_compressor_params Params;
        Params.m_pJob_pool = &JobPool;

        Params.m_source_images.resize(1);
        Params.m_source_images[0].init(Pixels.data(), Dimensions.x, Dimensions.y, 4);

        Params.m_uastc                      = true;
        Params.m_print_stats                = false;
        Params.m_mip_gen                    = true;
        Params.m_mip_fast                   = true;
        Params.m_multithreading             = true;
        Params.m_create_ktx2_file           = false;
        Params.m_quality_level              = 128;
        Params.m_pack_uastc_ldr_4x4_flags   = basisu::cPackUASTCLevelFastest;

        // Tell the encoder whether to optimize in perceptual (sRGB) or linear
        // error space. Mismatched perceptual mode + storage format is the
        // root cause of "all my albedos look dark" -- the encoder makes
        // perceptually-good bits, then the GPU samples them as linear and
        // every value comes out wrong. Mip filter color space matches.
        Params.m_perceptual = bIsSRGB;
        Params.m_mip_srgb   = bIsSRGB;

        // Normal maps don't need additional encoder hints here -- the BC5
        // transcode target below is what saves them. Basis encodes RGBA
        // UASTC; selecting BC5_RG at transcode time pulls only the RG
        // endpoints into the BC5 block, which is the same precision win as
        // a "2-channel mode" without a special encoder configuration.

        basisu::basis_compressor Compressor;
        if (!Compressor.init(Params))
        {
            return false;
        }
        if (Compressor.process() != basisu::basis_compressor::cECSuccess)
        {
            return false;
        }

        const basisu::uint8_vec& BasisData = Compressor.get_output_basis_file();
        basist::basisu_transcoder Transcoder;
        if (!Transcoder.start_transcoding(BasisData.data(), BasisData.size()))
        {
            return false;
        }

        basist::basisu_file_info FileInfo;
        Transcoder.get_file_info(BasisData.data(), BasisData.size(), FileInfo);
        const uint32 NumMips = FileInfo.m_image_mipmap_levels[0];

        basist::basisu_image_info ImageInfo;
        Transcoder.get_image_info(BasisData.data(), BasisData.size(), ImageInfo, 0);
        const uint32 Width  = ImageInfo.m_width;
        const uint32 Height = ImageInfo.m_height;

        // Format selection by color-space role:
        //   SRGB       -> BC7_UNORM_SRGB. GPU does sRGB->linear on every
        //                 fetch (free hardware path); critical for albedo
        //                 and any other color content.
        //   NormalMap  -> BC5_UNORM. Two-channel store (RG = X, Y); shader
        //                 reconstructs Z = sqrt(1 - x^2 - y^2). Doubles
        //                 angular precision vs. BC7_UNORM at the same byte
        //                 cost since BC5 dedicates both blocks to RG instead
        //                 of sharing endpoints across RGB.
        //   Linear /
        //   PackedData -> BC7_UNORM. Linear sampling, no perceptual decode.
        //                 BC4-per-channel splitting for ORM-style packs is
        //                 a future pass; BC7_UNORM with linear-mode encoding
        //                 is already a real improvement over the previous
        //                 "BC7 with perceptual encoder" mismatch.
        EFormat StoredFormat;
        basist::transcoder_texture_format TranscodeTarget;
        if (bIsSRGB)
        {
            StoredFormat    = EFormat::BC7_UNORM_SRGB;
            TranscodeTarget = basist::transcoder_texture_format::cTFBC7_RGBA;
        }
        else if (bIsNormalMap)
        {
            StoredFormat    = EFormat::BC5_UNORM;
            TranscodeTarget = basist::transcoder_texture_format::cTFBC5_RG;
        }
        else
        {
            StoredFormat    = EFormat::BC7_UNORM;
            TranscodeTarget = basist::transcoder_texture_format::cTFBC7_RGBA;
        }

        FRHIImageDesc ImageDescription;
        ImageDescription.Format            = StoredFormat;
        ImageDescription.Extent            = glm::uvec2(Width, Height);
        ImageDescription.Flags             .SetMultipleFlags(EImageCreateFlags::ShaderResource);
        ImageDescription.NumMips           = static_cast<uint8>(NumMips);
        ImageDescription.InitialState      = EResourceStates::ShaderResource;
        ImageDescription.bKeepInitialState = true;

        if (!Texture->TextureResource)
        {
            Texture->TextureResource = MakeUnique<FTextureResource>();
        }

        // Drop the previous bindless slot before swapping the RHI image; the
        // new image will re-register on PostLoad / fresh import.
        if (Texture->TextureResource->RHIImage && Texture->TextureResource->RHIImage->GetTextureCacheIndex() != -1)
        {
            GRenderManager->GetTextureManager().RemoveTexture(Texture->TextureResource->RHIImage);
        }

        Texture->TextureResource->ImageDescription = ImageDescription;
        Texture->TextureResource->Mips.clear();
        Texture->TextureResource->Mips.resize(NumMips);

        FRHIImageRef RHIImage = GRenderContext->CreateImage(ImageDescription);
        Texture->TextureResource->RHIImage = RHIImage;

        const uint32 BytesPerBlock = RHI::Format::BytesPerBlock(StoredFormat);

        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Compute());
        CommandList->Open();

        for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
        {
            basist::basisu_image_level_info LevelInfo;
            if (!Transcoder.get_image_level_info(BasisData.data(), BasisData.size(), LevelInfo, 0, MipIndex))
            {
                continue;
            }

            const uint32 BlocksX     = LevelInfo.m_num_blocks_x;
            const uint32 BlocksY     = LevelInfo.m_num_blocks_y;
            const uint32 TotalBlocks = LevelInfo.m_total_blocks;
            const uint32 RowPitch    = BlocksX * BytesPerBlock;
            const uint32 DepthPitch  = RowPitch * BlocksY;

            TVector<uint8> TranscodedData(TotalBlocks * BytesPerBlock);
            if (!Transcoder.transcode_image_level(
                    BasisData.data(), BasisData.size(),
                    0,
                    MipIndex,
                    TranscodedData.data(), TotalBlocks,
                    TranscodeTarget))
            {
                continue;
            }

            CommandList->WriteImage(RHIImage, 0, MipIndex, TranscodedData.data(), RowPitch, 1);

            FTextureResource::FMip& Mip = Texture->TextureResource->Mips[MipIndex];
            Mip.Width      = LevelInfo.m_width;
            Mip.Height     = LevelInfo.m_height;
            Mip.RowPitch   = RowPitch;
            Mip.Depth      = 1;
            Mip.SlicePitch = DepthPitch;
            Mip.Pixels     = Move(TranscodedData);
        }

        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList, ECommandQueue::Compute);

        // Re-register the new image so any binding tables / global texture
        // arrays pick it up. AddTexture is a no-op if the image is already
        // tracked, so it's safe to always call.
        GRenderManager->GetTextureManager().AddTexture(RHIImage);

        return true;
    }

    // Filename-driven default for ETextureColorSpace::Auto. The conventions
    // here match what the major DCC tools (Substance, Blender, Marmoset) export
    // by default. Anything we can't classify falls through to SRGB on the
    // assumption that "color textures are the common case"; the asset's
    // ColorSpace property is editable in the inspector for the cases the
    // heuristic gets wrong.
    static ETextureColorSpace ClassifyByFilename(FStringView Path)
    {
        eastl::string Stem(Path.data(), Path.size());

        // Strip any extension first so the suffix match isn't fooled by ".png".
        const size_t DotPos = Stem.find_last_of('.');
        if (DotPos != eastl::string::npos)
        {
            Stem.resize(DotPos);
        }
        // Lowercase the comparison region.
        for (char& C : Stem)
        {
            if (C >= 'A' && C <= 'Z') C = (char)(C + ('a' - 'A'));
        }

        auto EndsWith = [&Stem](const char* Suffix)
        {
            const size_t SufLen = strlen(Suffix);
            return Stem.size() >= SufLen && Stem.compare(Stem.size() - SufLen, SufLen, Suffix) == 0;
        };

        // Tangent-space normals.
        if (EndsWith("_n") || EndsWith("_normal") || EndsWith("_norm") || EndsWith("_nrm"))
            return ETextureColorSpace::NormalMap;

        // Multi-channel PBR packs (incl. glTF metallic-roughness convention,
        // which packs roughness in G and metallic in B with R unused or AO).
        if (EndsWith("_orm") || EndsWith("_arm") || EndsWith("_mra") || EndsWith("_rmo") ||
            EndsWith("_mro") || EndsWith("_rma") || EndsWith("_amr") ||
            EndsWith("_metalroughness") || EndsWith("_metallicroughness") ||
            EndsWith("_metalrough") || EndsWith("_mr") || EndsWith("_rm"))
            return ETextureColorSpace::PackedData;

        // Single-channel linear data textures.
        if (EndsWith("_r") || EndsWith("_rough") || EndsWith("_roughness") ||
            EndsWith("_m") || EndsWith("_metal") || EndsWith("_metallic") ||
            EndsWith("_ao") || EndsWith("_occ") || EndsWith("_occlusion") ||
            EndsWith("_h") || EndsWith("_height") || EndsWith("_disp") || EndsWith("_displacement"))
            return ETextureColorSpace::Linear;

        // Default for albedo / color / emissive / UI / unrecognized.
        return ETextureColorSpace::SRGB;
    }
    
    static void CreatePackageThumbnail(CTexture* Texture, const uint8* RawPixels, uint32 SourceWidth, uint32 SourceHeight)
    {
        CPackage* AssetPackage = Texture->GetPackage();
    
        constexpr uint32 ThumbWidth    = 256;
        constexpr uint32 ThumbHeight   = 256;
        constexpr uint32 BytesPerPixel = 4;
    
        AssetPackage->GetPackageThumbnail()->ImageWidth  = ThumbWidth;
        AssetPackage->GetPackageThumbnail()->ImageHeight = ThumbHeight;
        AssetPackage->GetPackageThumbnail()->ImageData.resize(ThumbWidth * ThumbHeight * BytesPerPixel);
    
        const uint32 SourceRowPitch = SourceWidth * BytesPerPixel;
        const uint8* SourceData     = RawPixels;
        uint8*       DestData       = AssetPackage->GetPackageThumbnail()->ImageData.data();
    
        const float ScaleX = static_cast<float>(SourceWidth)  / ThumbWidth;
        const float ScaleY = static_cast<float>(SourceHeight) / ThumbHeight;
    
        for (uint32 DestY = 0; DestY < ThumbHeight; ++DestY)
        {
            for (uint32 DestX = 0; DestX < ThumbWidth; ++DestX)
            {
                const float SrcX = DestX * ScaleX;
                const float SrcY = DestY * ScaleY;
    
                const uint32 X0 = static_cast<uint32>(SrcX);
                const uint32 Y0 = static_cast<uint32>(SrcY);
                const uint32 X1 = Math::Min(X0 + 1, SourceWidth  - 1);
                const uint32 Y1 = Math::Min(Y0 + 1, SourceHeight - 1);
    
                const float FracX = SrcX - X0;
                const float FracY = SrcY - Y0;
    
                const uint8* P00 = SourceData + (Y0 * SourceRowPitch) + (X0 * BytesPerPixel);
                const uint8* P10 = SourceData + (Y0 * SourceRowPitch) + (X1 * BytesPerPixel);
                const uint8* P01 = SourceData + (Y1 * SourceRowPitch) + (X0 * BytesPerPixel);
                const uint8* P11 = SourceData + (Y1 * SourceRowPitch) + (X1 * BytesPerPixel);
    
                const uint32 FlippedDestY = ThumbHeight - 1 - DestY;
                uint8* DestPixel = DestData + (FlippedDestY * ThumbWidth + DestX) * BytesPerPixel;
    
                for (uint32 Channel = 0; Channel < BytesPerPixel; ++Channel)
                {
                    const float Top    = Math::Lerp(static_cast<float>(P00[Channel]), static_cast<float>(P10[Channel]), FracX);
                    const float Bottom = Math::Lerp(static_cast<float>(P01[Channel]), static_cast<float>(P11[Channel]), FracX);
                    DestPixel[Channel] = static_cast<uint8>(std::lround(Math::Lerp(Top, Bottom, FracY)));
                }
            }
        }
    }
    
    void CTextureFactory::TryImport(const FFixedString& RawPath, const FFixedString& DestinationPath, const Import::FImportSettings* Settings)
    {
        CTexture* NewTexture = TryCreateNew<CTexture>(DestinationPath);
        NewTexture->SetFlag(OF_NeedsPostLoad);

        NewTexture->TextureResource = MakeUnique<FTextureResource>();
        
        TOptional<Import::Textures::FTextureImportResult> MaybeResult;
        const Import::Mesh::FMeshImportImage* ImageSettings = nullptr;

        if (Settings)
        {
            ImageSettings = &Settings->As<Import::Mesh::FMeshImportImage>();
        }

        // Bytes path = mesh-embedded image. File path = either a direct file
        // import (Settings == nullptr) or a mesh import that resolved a
        // relative URI to an absolute path (Settings != nullptr but no Bytes).
        // Both fall through to the same loader; only Settings.Bytes triggers
        // the in-memory path.
        if (ImageSettings && ImageSettings->IsBytes())
        {
            MaybeResult = Import::Textures::ImportTexture(ImageSettings->Bytes, false);
        }
        else
        {
            MaybeResult = Import::Textures::ImportTexture(RawPath, false);
        }

        if (!MaybeResult.has_value())
        {
            NewTexture->ForceDestroyNow();
            return;
        }

        const Import::Textures::FTextureImportResult& Result = MaybeResult.value();
        CreatePackageThumbnail(NewTexture, Result.Pixels.data(), Result.Dimensions.x, Result.Dimensions.y);

        // Apply mesh-importer-supplied semantic role first -- it's the most
        // accurate signal we have (came directly from glTF material slots /
        // assimp's aiTextureType), then fall back to filename pattern
        // matching for OBJ + standalone PNG drags. Auto is the wildcard;
        // any concrete value sticks regardless of filename.
        if (ImageSettings && ImageSettings->IntendedColorSpace != ETextureColorSpace::Auto)
        {
            NewTexture->ColorSpace = ImageSettings->IntendedColorSpace;
        }
        else if (NewTexture->ColorSpace == ETextureColorSpace::Auto)
        {
            NewTexture->ColorSpace = ClassifyByFilename(RawPath.c_str());
        }

        // Persist the source path so the editor's Recook button can rerun
        // compression after a ColorSpace change without forcing the user to
        // re-drag the file. Bytes-only imports (mesh-embedded textures) leave
        // SourcePath empty -- those can't be re-cooked from disk.
        if (!ImageSettings || !ImageSettings->IsBytes())
        {
            NewTexture->SourcePath = FString(RawPath.c_str());
        }

        TVector<uint8> Pixels = Move(Result.Pixels);

        if (!CookTexturePixels(NewTexture, Pixels, Result.Dimensions, NewTexture->ColorSpace))
        {
            NewTexture->ForceDestroyNow();
            return;
        }

        CPackage* NewPackage = NewTexture->GetPackage();
        if (CPackage::SavePackage(NewPackage, NewPackage->GetPackagePath()))
        {
            FAssetRegistry::Get().AssetCreated(NewTexture);
        }
        else
        {
            LOG_ERROR("TextureFactory: failed to save imported texture; asset will not be registered");
        }

        NewTexture->ConditionalBeginDestroy();
    }

    bool CTextureFactory::Recook(CTexture* Texture)
    {
        if (Texture == nullptr)
        {
            return false;
        }

        if (Texture->SourcePath.empty())
        {
            LOG_WARN("TextureFactory::Recook: '{0}' has no source path (likely mesh-embedded); cannot re-cook from disk.",
                     Texture->GetName().c_str());
            return false;
        }

        TOptional<Import::Textures::FTextureImportResult> MaybeResult = Import::Textures::ImportTexture(Texture->SourcePath, false);
        if (!MaybeResult.has_value())
        {
            LOG_WARN("TextureFactory::Recook: failed to load source file '{0}' for '{1}'.",
                     Texture->SourcePath.c_str(), Texture->GetName().c_str());
            return false;
        }

        const Import::Textures::FTextureImportResult& Result = MaybeResult.value();
        TVector<uint8> Pixels = Move(Result.Pixels);

        // ColorSpace::Auto on a re-cook is treated the same as a fresh
        // import: classify by filename and persist the resolved value so the
        // user sees the concrete role in the inspector after re-cooking.
        if (Texture->ColorSpace == ETextureColorSpace::Auto)
        {
            Texture->ColorSpace = ClassifyByFilename(Texture->SourcePath);
        }

        if (!CookTexturePixels(Texture, Pixels, Result.Dimensions, Texture->ColorSpace))
        {
            return false;
        }

        if (CPackage* Package = Texture->GetPackage())
        {
            Package->MarkDirty();
        }

        return true;
    }

}
