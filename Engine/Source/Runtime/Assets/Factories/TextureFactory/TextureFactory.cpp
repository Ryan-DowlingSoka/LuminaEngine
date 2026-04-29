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
#include <glm/gtc/packing.hpp>

namespace Lumina
{
    CObject* CTextureFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        return NewObject<CTexture>(Package, Name);
    }

    // ----------------------------------------------------------------------------
    //  HDR / Environment cooking path
    //
    //  Bypasses Basis Universal entirely: Basis is LDR-only (it expects sRGB
    //  RGBA8 input and clamps anything beyond [0,1]), so feeding a radiance
    //  HDR through it would burn the brightest values to white before the
    //  IBL convolution ever sees them. Instead, store as RGBA16F: half the
    //  memory of float32, more than enough precision for environment lighting
    //  (the IBL pipeline takes thousands of samples per output texel and any
    //  half-float quantization noise averages out), and natively sampled by
    //  Vulkan without a transcode step.
    //
    //  No mips: the equirect is an intermediate the renderer converts to a
    //  cubemap on first use; the cube then gets its own GGX prefilter chain
    //  in PrefilterEnvMapPass.
    // ----------------------------------------------------------------------------
    static bool CookEnvironmentTexture(CTexture* Texture, const Import::Textures::FTextureImportResult& Source)
    {
        const uint32 Width  = Source.Dimensions.x;
        const uint32 Height = Source.Dimensions.y;
        const uint64 NumTexels = uint64(Width) * uint64(Height);
        if (NumTexels == 0)
        {
            return false;
        }

        // Source is always float32 here -- TextureFactory routes only files
        // that loaded as one of the float formats below into this path.
        uint32 SrcChannels = 0;
        switch (Source.Format)
        {
            case EFormat::R32_FLOAT:    SrcChannels = 1; break;
            case EFormat::RG32_FLOAT:   SrcChannels = 2; break;
            case EFormat::RGB32_FLOAT:  SrcChannels = 3; break;
            case EFormat::RGBA32_FLOAT: SrcChannels = 4; break;
            default:
                LOG_WARN("CookEnvironmentTexture: '{0}' isn't a float source; Environment color space requires .hdr",
                         Texture->GetName().c_str());
                return false;
        }

        const float* SrcFloats = reinterpret_cast<const float*>(Source.Pixels.data());

        // RGBA16F packed as two uint32 per pixel: low halves = (R, G), high
        // halves = (B, A). glm::packHalf2x16 puts the first vec2 component in
        // the low 16 bits, which matches RGBA16F's natural little-endian
        // byte order (R first in memory). 8 bytes per pixel.
        TVector<uint32> Halves(NumTexels * 2);
        for (uint64 i = 0; i < NumTexels; ++i)
        {
            const float* Src = SrcFloats + i * SrcChannels;
            const float R = SrcChannels >= 1 ? Src[0] : 0.0f;
            const float G = SrcChannels >= 2 ? Src[1] : 0.0f;
            const float B = SrcChannels >= 3 ? Src[2] : 0.0f;
            const float A = SrcChannels >= 4 ? Src[3] : 1.0f;
            Halves[i * 2 + 0] = glm::packHalf2x16(glm::vec2(R, G));
            Halves[i * 2 + 1] = glm::packHalf2x16(glm::vec2(B, A));
        }

        FRHIImageDesc ImageDescription;
        ImageDescription.Format            = EFormat::RGBA16_FLOAT;
        ImageDescription.Extent            = glm::uvec2(Width, Height);
        ImageDescription.Flags             .SetMultipleFlags(EImageCreateFlags::ShaderResource);
        ImageDescription.NumMips           = 1;
        ImageDescription.InitialState      = EResourceStates::ShaderResource;
        ImageDescription.bKeepInitialState = true;

        if (!Texture->TextureResource)
        {
            Texture->TextureResource = MakeUnique<FTextureResource>();
        }

        if (Texture->TextureResource->RHIImage && Texture->TextureResource->RHIImage->GetTextureCacheIndex() != -1)
        {
            GRenderManager->GetTextureManager().RemoveTexture(Texture->TextureResource->RHIImage);
        }

        Texture->TextureResource->ImageDescription = ImageDescription;
        Texture->TextureResource->Mips.clear();
        Texture->TextureResource->Mips.resize(1);

        FRHIImageRef RHIImage = GRenderContext->CreateImage(ImageDescription);
        Texture->TextureResource->RHIImage = RHIImage;

        const uint32 BytesPerPixel = 8u;  // RGBA16F.
        const uint32 RowPitch      = Width * BytesPerPixel;
        const uint32 SlicePitch    = RowPitch * Height;

        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Compute());
        CommandList->Open();
        CommandList->WriteImage(RHIImage, 0, 0, Halves.data(), RowPitch, 1);
        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList, ECommandQueue::Compute);

        FTextureResource::FMip& Mip = Texture->TextureResource->Mips[0];
        Mip.Width      = Width;
        Mip.Height     = Height;
        Mip.RowPitch   = RowPitch;
        Mip.Depth      = 1;
        Mip.SlicePitch = SlicePitch;
        Mip.Pixels.assign(reinterpret_cast<uint8*>(Halves.data()),
                          reinterpret_cast<uint8*>(Halves.data()) + SlicePitch);

        GRenderManager->GetTextureManager().AddTexture(RHIImage);
        return true;
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

        // Lowercase before slicing so the .hdr extension check is case-
        // insensitive (matters: HDRIHaven and Polyhaven exports often
        // ship as .HDR).
        for (char& C : Stem)
        {
            if (C >= 'A' && C <= 'Z') C = (char)(C + ('a' - 'A'));
        }

        // .hdr is always an HDR equirectangular panorama in practice;
        // route it to the Environment cooking path before any of the
        // suffix-based heuristics below see it.
        if (Stem.size() >= 4 && Stem.compare(Stem.size() - 4, 4, ".hdr") == 0)
        {
            return ETextureColorSpace::Environment;
        }

        // Strip any extension so the suffix match isn't fooled by ".png".
        const size_t DotPos = Stem.find_last_of('.');
        if (DotPos != eastl::string::npos)
        {
            Stem.resize(DotPos);
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
    
#if USING(WITH_EDITOR)
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
#endif

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

        // Hard override for float-source data: Basis Universal expects
        // 8-bit RGBA and would interpret the float bytes as garbage 8-bit
        // pixels, producing a corrupt asset with no error. Anything that
        // loads as a float format MUST take the Environment path. The
        // CTexture default ColorSpace is SRGB, which would otherwise win
        // here because the filename heuristic only fires on Auto.
        const bool bIsFloatSource =
            Result.Format == EFormat::R32_FLOAT    ||
            Result.Format == EFormat::RG32_FLOAT   ||
            Result.Format == EFormat::RGB32_FLOAT  ||
            Result.Format == EFormat::RGBA32_FLOAT;
        if (bIsFloatSource)
        {
            NewTexture->ColorSpace = ETextureColorSpace::Environment;
        }

#if USING(WITH_EDITOR)
        // Thumbnail generator assumes 4-byte RGBA8 strides. HDR float inputs
        // would read garbage (the first byte of each float, treated as 8-bit
        // color) and produce a broken-looking preview. Skip it for HDR;
        // package gets a default thumbnail.
        if (NewTexture->ColorSpace != ETextureColorSpace::Environment)
        {
            CreatePackageThumbnail(NewTexture, Result.Pixels.data(), Result.Dimensions.x, Result.Dimensions.y);
        }
#endif

        // Persist the source path so the editor's Recook button can rerun
        // compression after a ColorSpace change without forcing the user to
        // re-drag the file. Bytes-only imports (mesh-embedded textures) leave
        // SourcePath empty -- those can't be re-cooked from disk.
        if (!ImageSettings || !ImageSettings->IsBytes())
        {
            NewTexture->SourcePath = FString(RawPath.c_str());
        }

        bool bCooked = false;
        if (NewTexture->ColorSpace == ETextureColorSpace::Environment)
        {
            // Skip the Basis/BC* path entirely -- HDR data needs to stay HDR.
            bCooked = CookEnvironmentTexture(NewTexture, Result);
        }
        else
        {
            TVector<uint8> Pixels = Move(Result.Pixels);
            bCooked = CookTexturePixels(NewTexture, Pixels, Result.Dimensions, NewTexture->ColorSpace);
        }

        if (!bCooked)
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

        // ColorSpace::Auto on a re-cook is treated the same as a fresh
        // import: classify by filename and persist the resolved value so the
        // user sees the concrete role in the inspector after re-cooking.
        if (Texture->ColorSpace == ETextureColorSpace::Auto)
        {
            Texture->ColorSpace = ClassifyByFilename(Texture->SourcePath);
        }

        // Same hard override as TryImport: float-source data can't go
        // through Basis. If a user manually flips ColorSpace away from
        // Environment on an .hdr asset we still cook it correctly.
        const bool bIsFloatSource =
            Result.Format == EFormat::R32_FLOAT    ||
            Result.Format == EFormat::RG32_FLOAT   ||
            Result.Format == EFormat::RGB32_FLOAT  ||
            Result.Format == EFormat::RGBA32_FLOAT;
        if (bIsFloatSource)
        {
            Texture->ColorSpace = ETextureColorSpace::Environment;
        }

        bool bCooked = false;
        if (Texture->ColorSpace == ETextureColorSpace::Environment)
        {
            bCooked = CookEnvironmentTexture(Texture, Result);
        }
        else
        {
            TVector<uint8> Pixels = Move(Result.Pixels);
            bCooked = CookTexturePixels(Texture, Pixels, Result.Dimensions, Texture->ColorSpace);
        }

        if (!bCooked)
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
