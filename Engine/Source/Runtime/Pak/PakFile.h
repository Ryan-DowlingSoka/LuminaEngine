#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // On-disk layout
    // ------------------------------------------------------------------
    //   FPakHeader              (fixed-size, at file start)
    //     uint32 Magic   = 'LPAK'  (0x4B41504C little-endian)
    //     uint32 Version = 1
    //     uint64 TocOffset
    //     uint32 EntryCount
    //   <concatenated entry blobs, no padding>
    //   <TOC at TocOffset>
    //     for each entry (EntryCount times):
    //       uint32 PathLen
    //       char[PathLen] Path     (UTF-8, no null terminator)
    //       uint64 Offset          (absolute byte offset of entry data)
    //       uint64 Size            (byte length of entry data)
    //
    // The TOC sits at the end so a single streaming write can append blobs
    // without seeking, then patch only the header at the end.

    static constexpr uint32 PAK_MAGIC   = 0x4B41504Cu; // 'LPAK'
    static constexpr uint32 PAK_VERSION = 1u;

    struct FPakHeader
    {
        uint32 Magic;
        uint32 Version;
        uint64 TocOffset;
        uint32 EntryCount;
        uint32 _Pad;            // keeps the struct 8-byte aligned for future fields
    };
    static_assert(sizeof(FPakHeader) == 24, "FPakHeader on-disk size must be stable");

    struct FPakEntry
    {
        uint64 Offset = 0;
        uint64 Size   = 0;
    };
}
