#pragma once

#include "Core/Object/Object.h"
#include "Memory/RefCounted.h"
#include "Renderer/RenderResource.h"
#include "Texture.generated.h"
#include "Memory/SmartPtr.h"
#include "Renderer/TextureData.h"

namespace Lumina
{
    /**
     * Semantic role of a texture's contents. Drives format selection at import
     * time (sRGB encoding for color, linear for everything else) and tells the
     * Basis Universal encoder which error metric to optimize for.
     *
     * The runtime never stretches sRGB-encoded data through linear math: a
     * texture marked SRGB is stored in a *_UNORM_SRGB Vulkan format so the
     * GPU's hardware sampler does the gamma decode on read. Linear, NormalMap,
     * and PackedData all bypass that decode (the bits ARE the values).
     */
    REFLECT()
    enum class ETextureColorSpace : uint8
    {
        // Filename-derived heuristic; resolved at import time and never
        // serialized as Auto -- the import path always writes back one of
        // the concrete entries below.
        Auto,

        // Linear-encoded, color-correct interpretation. For data textures
        // whose channels happen to be 3-component but aren't perceptual
        // colors (custom mask textures, etc).
        Linear,

        // sRGB-encoded color (albedo, emissive, UI). The default for
        // unrecognized filenames.
        SRGB,

        // Tangent-space normal map. Treated as linear today; future work
        // will switch this branch to BC5_UNORM with shader-side Z reconstruct.
        NormalMap,

        // Packed PBR data (ORM/MRA/ARM/etc) -- multiple independent linear
        // channels. Treated as linear today; future work will switch this
        // branch to per-channel BC4 to preserve inter-channel precision.
        PackedData,

        // HDR equirectangular panorama for image-based lighting. Stored
        // uncompressed in float16 (R16G16B16A16_SFLOAT) so the values
        // delivered to the IBL convolution are still radiances, not LDR
        // approximations. Bypasses Basis Universal (which is LDR-only)
        // and the BC* compressed formats. Drag a .hdr onto the editor
        // and set ColorSpace to Environment to use it as the scene's
        // sky source via SEnvironmentComponent::EnvironmentMap.
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

        /** How the texture's contents should be interpreted at sample time. */
        PROPERTY(Editable)
        ETextureColorSpace ColorSpace = ETextureColorSpace::SRGB;

        /** Absolute path to the original asset file the texture was imported
         *  from. Persisted so the editor can re-cook the texture after the
         *  user changes ColorSpace (or any future cooking-relevant property)
         *  without forcing them to re-drag the source file. Empty for
         *  textures created without a source file (e.g. mesh-embedded). */
        PROPERTY()
        FString SourcePath;

        TUniquePtr<FTextureResource> TextureResource;

        int64 GlobalTextureIndex;
    };
}
