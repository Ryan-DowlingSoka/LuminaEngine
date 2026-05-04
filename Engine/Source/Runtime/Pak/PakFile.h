#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // On-disk: Header | concatenated (possibly compressed) blobs | TOC at TocOffset.
    // TOC entries: u32 PathLen, char[PathLen] Path, u64 Offset, u64 CompressedSize,
    //              u64 UncompressedSize, u8 Method, u8 Pad[7].
    // TOC is last so blobs append without seeking.

    static constexpr uint32 PAK_MAGIC   = 0x4B41504Cu; // 'LPAK'
    static constexpr uint32 PAK_VERSION = 2u;

    // Compression payoff threshold; entries below this are stored raw.
    static constexpr size_t PAK_COMPRESSION_MIN_SIZE = 256;

    enum class EPakCompression : uint8
    {
        None    = 0,
        Deflate = 1,
    };

    struct FPakHeader
    {
        uint32 Magic;
        uint32 Version;
        uint64 TocOffset;
        uint32 EntryCount;
        uint32 _Pad;            // 8-byte alignment
    };
    static_assert(sizeof(FPakHeader) == 24, "FPakHeader on-disk size must be stable");

    // Runtime entry: points into the archive's decompressed buffer.
    struct FPakEntry
    {
        uint64 Offset = 0;
        uint64 Size   = 0;
    };
}
