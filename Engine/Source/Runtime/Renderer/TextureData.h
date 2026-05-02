#pragma once
#include "RenderResource.h"
#include "Containers/Array.h"

namespace Lumina
{
    struct FTextureResource
    {
        struct FMip
        {
            uint32 Width;
            uint32 Height;
            uint32 Depth;
            uint32 RowPitch;
            uint32 SlicePitch;
            TVector<uint8> Pixels;
        };
        
        FRHIImageDesc           ImageDescription;
        FRHIImageRef            RHIImage;
        TFixedVector<FMip, 1>   Mips;

        uint64 CalcTotalSizeBytes() const
        {
            uint64 BytesPerBlock = RHI::Format::BytesPerBlock(ImageDescription.Format);
            uint64 TotalSize = 0;
            
            for (const FMip& Mip : Mips)
            {
                TotalSize += (uint64)Mip.RowPitch * Mip.Height * Mip.Depth;
            }

            return TotalSize;
        }

        friend FArchive& operator << (FArchive& Ar, FTextureResource& Data)
        {
            Ar << Data.ImageDescription;

            if (Ar.IsReading())
            {
                Data.Mips.clear();
                Data.Mips.resize(Data.ImageDescription.NumMips);
            }
            
            for (FMip& Mip : Data.Mips)
            {
                Ar << Mip.Width;
                Ar << Mip.Height;
                Ar << Mip.Depth;
                Ar << Mip.RowPitch;
                Ar << Mip.SlicePitch;
                Ar << Mip.Pixels;
            }
            
            return Ar;
        }
    };
}
