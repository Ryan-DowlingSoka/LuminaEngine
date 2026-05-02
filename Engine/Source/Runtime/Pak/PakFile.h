#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // On-disk: Header | concatenated blobs | TOC at TocOffset.
    // TOC entries: u32 PathLen, char[PathLen] Path, u64 Offset, u64 Size.
    // TOC is last so blobs append without seeking.

    static constexpr uint32 PAK_MAGIC   = 0x4B41504Cu; // 'LPAK'
    static constexpr uint32 PAK_VERSION = 1u;

    struct FPakHeader
    {
        uint32 Magic;
        uint32 Version;
        uint64 TocOffset;
        uint32 EntryCount;
        uint32 _Pad;            // 8-byte alignment
    };
    static_assert(sizeof(FPakHeader) == 24, "FPakHeader on-disk size must be stable");

    struct FPakEntry
    {
        uint64 Offset = 0;
        uint64 Size   = 0;
    };
}
