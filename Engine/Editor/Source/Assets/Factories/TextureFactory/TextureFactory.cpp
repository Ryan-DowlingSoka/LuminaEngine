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
#include "Core/Math/Math.h"

namespace Lumina
{
    CObject* CTextureFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        return NewObject<CTexture>(Package, Name);
    }

    // HDR/Environment cook: bypasses Basis (LDR-only) and stores RGBA16F. No mips; the cube prefilter chain owns mip generation.
    static bool CookEnvironmentTexture(CTexture* Texture, const Import::Textures::FTextureImportResult& Source)
    {
        const uint32 Width  = Source.Dimensions.x;
        const uint32 Height = Source.Dimensions.y;
        const uint64 NumTexels = uint64(Width) * uint64(Height);
        if (NumTexels == 0)
        {
            return false;
        }

        // Source is always float32 here; only float-format files reach this path.
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

        // RGBA16F: two uint32 per pixel; (R,G) low half, (B,A) high half. 8 bytes per pixel.
        TVector<uint32> Halves(NumTexels * 2);
        for (uint64 i = 0; i < NumTexels; ++i)
        {
            const float* Src = SrcFloats + i * SrcChannels;
            const float R = SrcChannels >= 1 ? Src[0] : 0.0f;
            const float G = SrcChannels >= 2 ? Src[1] : 0.0f;
            const float B = SrcChannels >= 3 ? Src[2] : 0.0f;
            const float A = SrcChannels >= 4 ? Src[3] : 1.0f;
            Halves[i * 2 + 0] = Math::PackHalf2x16(FVector2(R, G));
            Halves[i * 2 + 1] = Math::PackHalf2x16(FVector2(B, A));
        }

        FRHIImageDesc ImageDescription;
        ImageDescription.Format            = EFormat::RGBA16_FLOAT;
        ImageDescription.Extent            = FUIntVector2(Width, Height);
        ImageDescription.Flags             .SetMultipleFlags(EImageCreateFlags::ShaderResource);
        ImageDescription.NumMips           = 1;
        ImageDescription.InitialState      = EResourceStates::ShaderResource;
        ImageDescription.bKeepInitialState = true;

        if (!Texture->TextureResource)
        {
            Texture->TextureResource = MakeUnique<FTextureResource>();
        }

        Texture->TextureResource->ImageDescription = ImageDescription;
        Texture->TextureResource->Mips.clear();
        Texture->TextureResource->Mips.resize(1);

        FRHIImageRef RHIImage = GRenderContext->CreateImage(ImageDescription);
        Texture->TextureResource->RHIImage = RHIImage;

        const uint32 BytesPerPixel = 8u;
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

        return true;
    }

    // Encodes RGBA8 via Basis Universal; shared by initial import and Recook.
    static bool CookTexturePixels(CTexture* Texture, const TVector<uint8>& Pixels, FUIntVector2 Dimensions, ETextureColorSpace ColorSpace)
    {
        // basisu's encoder tables are per-DLL static; FEngine::Init's init runs in Runtime,
        // so init the Editor's copy here. Idempotent (mutex + g_library_initialized).
        basisu::basisu_encoder_init();

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

        // Perceptual mode must match storage format or sRGB albedos look wrong.
        Params.m_perceptual = bIsSRGB;
        Params.m_mip_srgb   = bIsSRGB;

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

        // SRGB->BC7_UNORM_SRGB; NormalMap->BC5_UNORM (shader reconstructs Z); Linear/Packed->BC7_UNORM.
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
        ImageDescription.Extent            = FUIntVector2(Width, Height);
        ImageDescription.Flags             .SetMultipleFlags(EImageCreateFlags::ShaderResource);
        ImageDescription.NumMips           = static_cast<uint8>(NumMips);
        ImageDescription.InitialState      = EResourceStates::ShaderResource;
        ImageDescription.bKeepInitialState = true;

        if (!Texture->TextureResource)
        {
            Texture->TextureResource = MakeUnique<FTextureResource>();
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

        return true;
    }

    // Filename suffix heuristic for Auto; falls back to SRGB. Editable in the inspector for misclassifications.
    static ETextureColorSpace ClassifyByFilename(FStringView Path)
    {
        eastl::string Stem(Path.data(), Path.size());

        // Lowercase first so .HDR/.hdr both match.
        for (char& C : Stem)
        {
            if (C >= 'A' && C <= 'Z') C = (char)(C + ('a' - 'A'));
        }

        // Route .hdr to Environment before suffix heuristics run.
        if (Stem.size() >= 4 && Stem.compare(Stem.size() - 4, 4, ".hdr") == 0)
        {
            return ETextureColorSpace::Environment;
        }

        // Strip extension before suffix match.
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

        if (EndsWith("_n") || EndsWith("_normal") || EndsWith("_norm") || EndsWith("_nrm"))
            return ETextureColorSpace::NormalMap;

        if (EndsWith("_orm") || EndsWith("_arm") || EndsWith("_mra") || EndsWith("_rmo") ||
            EndsWith("_mro") || EndsWith("_rma") || EndsWith("_amr") ||
            EndsWith("_metalroughness") || EndsWith("_metallicroughness") ||
            EndsWith("_metalrough") || EndsWith("_mr") || EndsWith("_rm"))
            return ETextureColorSpace::PackedData;

        if (EndsWith("_r") || EndsWith("_rough") || EndsWith("_roughness") ||
            EndsWith("_m") || EndsWith("_metal") || EndsWith("_metallic") ||
            EndsWith("_ao") || EndsWith("_occ") || EndsWith("_occlusion") ||
            EndsWith("_h") || EndsWith("_height") || EndsWith("_disp") || EndsWith("_displacement"))
            return ETextureColorSpace::Linear;

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

        // Bytes path = mesh-embedded; file path = direct or mesh-resolved URI.
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
            // Refcount-safe teardown (matches the success path below): frees the unreferenced transient
            // texture, but won't dangle a holder if one ever takes a ref.
            NewTexture->ConditionalBeginDestroy();
            return;
        }

        const Import::Textures::FTextureImportResult& Result = MaybeResult.value();

        // Mesh-supplied role wins; otherwise classify by filename for direct drags.
        if (ImageSettings && ImageSettings->IntendedColorSpace != ETextureColorSpace::Auto)
        {
            NewTexture->ColorSpace = ImageSettings->IntendedColorSpace;
        }
        else if (NewTexture->ColorSpace == ETextureColorSpace::Auto)
        {
            NewTexture->ColorSpace = ClassifyByFilename(RawPath.c_str());
        }

        // Float-source data must take the Environment path; Basis would silently corrupt it.
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
        // Thumbnail generator assumes RGBA8; skip for HDR.
        if (NewTexture->ColorSpace != ETextureColorSpace::Environment)
        {
            CreatePackageThumbnail(NewTexture, Result.Pixels.data(), Result.Dimensions.x, Result.Dimensions.y);
        }
#endif

        // Persist source path for Recook; bytes-only imports leave it empty.
        if (!ImageSettings || !ImageSettings->IsBytes())
        {
            NewTexture->SourcePath = FString(RawPath.c_str());
        }

        bool bCooked = false;
        if (NewTexture->ColorSpace == ETextureColorSpace::Environment)
        {
            bCooked = CookEnvironmentTexture(NewTexture, Result);
        }
        else
        {
            TVector<uint8> Pixels = Move(Result.Pixels);
            bCooked = CookTexturePixels(NewTexture, Pixels, Result.Dimensions, NewTexture->ColorSpace);
        }

        if (!bCooked)
        {
            NewTexture->ConditionalBeginDestroy();
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

        // Re-cook with Auto resolves like a fresh import.
        if (Texture->ColorSpace == ETextureColorSpace::Auto)
        {
            Texture->ColorSpace = ClassifyByFilename(Texture->SourcePath);
        }

        // Float-source data must stay on the Environment path even if user changed ColorSpace.
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
