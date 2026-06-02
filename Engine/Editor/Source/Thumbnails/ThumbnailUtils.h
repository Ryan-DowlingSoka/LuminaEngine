#pragma once
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    struct FPackageThumbnail;

    namespace ThumbnailUtils
    {
        // Square resolution every generated thumbnail is stored at.
        inline constexpr uint32 kThumbnailResolution = 256;

        // Bilinear-downsample a mapped RGBA8 source (RowPitch bytes per row) into Thumbnail.ImageData at
        // kThumbnailResolution. Stored vertically flipped; the GPU-upload path flips it back for display.
        // Resets LoadState to None so the next display query re-uploads. Shared by every capture path.
        void StoreDownsampledRGBA(FPackageThumbnail& Thumbnail, const uint8* Source,
                                  uint32 SourceWidth, uint32 SourceHeight, size_t RowPitch);
    }
}
