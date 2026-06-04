#pragma once

#include "Core/Object/Object.h"
#include "Memory/RefCounted.h"
#include "Memory/SmartPtr.h"
#include "Renderer/RenderResource.h"
#include "Renderer/TextureData.h"
#include "Texture.generated.h"

namespace Lumina
{
    /** Drives import-time format selection; SRGB textures use *_UNORM_SRGB so the sampler does gamma decode. */
    REFLECT()
    enum class ETextureColorSpace : uint8
    {
        // Filename-derived heuristic; resolved at import time and rewritten to a concrete entry.
        Auto,

        // Linear-encoded data textures (custom masks, etc).
        Linear,

        // sRGB color (albedo, emissive, UI). Default for unrecognized filenames.
        SRGB,

        // Tangent-space normal map; stored BC5_UNORM (RG), shader reconstructs Z.
        NormalMap,

        // Packed PBR data (ORM/MRA/etc); stored BC7_UNORM.
        PackedData,

        // HDR equirectangular panorama; stored uncompressed float16 so IBL convolution sees radiances.
        Environment,
    };

    REFLECT()
    class RUNTIME_API CTexture : public CObject
    {
        GENERATED_BODY()

    public:

        void Serialize(FArchive& Ar) override;
        void PreLoad() override;
        void PostLoad() override;
        void OnDestroy() override;
        bool IsAsset() const override { return true; }


        FORCEINLINE FRHIImage* GetRHIRef() const { return TextureResource.get() ? TextureResource->RHIImage : nullptr; }
        FTextureResource& GetTextureResource() const { return *TextureResource.get(); }
        uint8 GetNumMips() const { return TextureResource.get() ? TextureResource->Mips.size() : 0u; }

        PROPERTY(Editable)
        ETextureColorSpace ColorSpace = ETextureColorSpace::SRGB;

        /** Source path persisted so the editor can re-cook after ColorSpace changes; empty for embedded. */
        PROPERTY()
        FString SourcePath;

        TUniquePtr<FTextureResource> TextureResource;

        int64 GlobalTextureIndex;
    };
}
