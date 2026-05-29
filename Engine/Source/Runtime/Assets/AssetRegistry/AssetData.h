#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "GUID/GUID.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    // Classification of edges in the asset dependency graph. The cooker
    // honors these to decide what to traverse and what to ship.
    enum class EDependencyType : uint8
    {
        // Default. Always followed; target lands in the same chunk as
        // (or a chunk reachable by) the referrer.
        Hard         = 0,

        // Optional/async ref (TSoftObjectPtr<T>, FSoftObjectPath). Followed
        // only to mark the target reachable; chunk is decided by the
        // target's own roots, not the referrer's. Phase 2 will wire the
        // C++ pointer types; for Phase 1 this exists to be future-proof.
        Soft         = 1,

        // Discovered via script analysis (Asset.Soft / RequireAsset etc).
        // Treated like Soft at cook time.
        Script       = 2,

        // Referrer owns target; cooker forces them into the same chunk
        // and the editor refuses orphan delete without parent acknowledgement.
        Owned        = 3,

        // Editor tooling reference (preview, thumbnail). Pruned in cook.
        EditorOnly   = 4,

        // Generated at runtime, not on disk. Recorded for diagnostics
        // but not followed by cook.
        Generated    = 5,
    };

    RUNTIME_API FStringView LexToString(EDependencyType T);

    // Per-asset cook flags. Stored in FAssetData::Flags. Sourced from
    // class-level REFLECT(Class, ...) metadata + per-asset overrides
    // baked into the package header.
    enum class EAssetFlags : uint32
    {
        None         = 0,
        EditorOnly   = 1 << 0,  // Never enters cooked output.
        RuntimeOnly  = 1 << 1,  // Skipped during editor-mode registration.
        AlwaysCook   = 1 << 2,  // Implicit root; included even without inbound ref.
        NeverCook    = 1 << 3,  // Forbid cooking; report error if reached.
        Primary      = 1 << 4,  // Auto-root for Asset Manager (Phase 2).
    };

    constexpr EAssetFlags operator|(EAssetFlags a, EAssetFlags b)
    {
        return static_cast<EAssetFlags>(static_cast<uint32>(a) | static_cast<uint32>(b));
    }
    constexpr EAssetFlags operator&(EAssetFlags a, EAssetFlags b)
    {
        return static_cast<EAssetFlags>(static_cast<uint32>(a) & static_cast<uint32>(b));
    }
    constexpr bool HasFlag(EAssetFlags Set, EAssetFlags F)
    {
        return (static_cast<uint32>(Set) & static_cast<uint32>(F)) != 0;
    }

    // One outbound edge in the dependency graph. Direct refs only;
    // transitive closure is computed by FCookGraph at cook time.
    struct FAssetDependency
    {
        FGuid           TargetGUID;
        EDependencyType Type = EDependencyType::Hard;

        bool operator==(const FAssetDependency& Other) const
        {
            return TargetGUID == Other.TargetGUID && Type == Other.Type;
        }
    };

    struct FAssetData
    {
        FGuid                       AssetGUID;
        FFixedString                Path;
        FName                       AssetName;
        FName                       AssetClass;

        // xxhash of the asset's raw .lasset bytes on disk. Used by the
        // registry to skip re-extraction on unchanged files, and by the
        // cooker as the per-asset input fingerprint (Phase 3 DDC key).
        uint64                      ContentHash    = 0;

        // Source file mtime as a unix-epoch nanos value. Cheap pre-check
        // before computing ContentHash on every discovery pass.
        int64                       SourceMTimeNs  = 0;

        EAssetFlags                 Flags          = EAssetFlags::None;

        // Direct outbound references (extracted from the package's
        // ImportTable at discovery + reflection-typed where possible).
        // Transitive reachability is FCookGraph's job, not stored here.
        TVector<FAssetDependency>   Dependencies;

        // Default-chunk hint declared by the owning plugin / project
        // (Phase 3 wires actual chunk assignment from cook roots).
        FName                       OwnerChunk;

        // Empty for /Game and /Engine; set to the plugin name for
        // /<PluginName>/* so the cooker can group plugin content.
        FName                       OwningPlugin;
    };
}
