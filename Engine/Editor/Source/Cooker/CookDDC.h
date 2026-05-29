#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Platform/GenericPlatform.h"


namespace Lumina
{
    /** Stable key for a cooked artifact. Derived from inputs that affect
     *  the cooker's output for one asset: source content hash + cooker
     *  version stamp + cook-mode flags. Bump `FCookDDC::kCookStamp` when
     *  cook logic changes in a way that should invalidate cached bytes. */
    struct FCookInputHash
    {
        uint64 Hash = 0;

        bool IsValid() const { return Hash != 0; }
        bool operator==(const FCookInputHash& O) const { return Hash == O.Hash; }
        bool operator!=(const FCookInputHash& O) const { return Hash != O.Hash; }
    };

    /** Disk-backed derived data cache for the cooker. Lives under
     *  `<EngineInstall>/Intermediates/DDC/<XX>/<full-hash>.ddc`. Two-byte
     *  hex prefix splits the cache into 256 buckets so directory sizes
     *  stay manageable as content grows.
     *
     *  Hit means: identical source content + identical cook logic, so the
     *  cached bytes are exactly what BundleAssetCooked would produce
     *  fresh. On miss the cooker does the full load + cook-mode resave
     *  and `Put`s the result for next time. */
    class FCookDDC
    {
    public:
        // Bump to invalidate every cached entry the next cook reads.
        // Suitable triggers: changes to CStruct::SerializeTaggedProperties
        // cook-mode behavior, FPackageSaver layout changes, FObjectImport
        // wire format, EditorOnly strip rules, etc.
        //
        // 1: initial DDC implementation
        // 2: Phase 4A EditorOnly strip via SavePackageForCook (cook-mode
        //    saver drops EditorOnly properties + thumbnails), Phase 3C
        //    sorted soft-import emission, Phase 5 PAK v3 layout
        static constexpr uint32 kCookStamp = 2;

        static FCookInputHash ComputeKey(uint64 SourceContentHash);

        // Returns true and fills OutBytes on hit; bumps the hit counter.
        // Bumps the miss counter on absence (so the caller doesn't need to).
        static bool TryGet(const FCookInputHash& Key, TVector<uint8>& OutBytes);

        // Idempotent; safe to call concurrently from cook tasks.
        static bool Put(const FCookInputHash& Key, const TVector<uint8>& Bytes);

        // Per-Cook() diagnostics; reset at the top of FAssetCooker::Cook.
        static void Reset();
        static size_t Hits();
        static size_t Misses();
        static size_t WrittenBytes();
    };
}
