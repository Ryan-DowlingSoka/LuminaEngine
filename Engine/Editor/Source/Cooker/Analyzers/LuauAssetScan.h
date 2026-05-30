#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"


namespace Lumina
{
    class FAssetRegistry;

    // Scans .luau for Asset.Hard/Soft/LoadAsync + absolute require("/…") call sites, resolving to registered FAssetData; follows require() chains transitively and folds results in as implicit Script cook roots.
    // Pattern-based, not a real parser: only string-literal args detected; dynamic paths and Luau-relative requires are missed.
    class FLuauAssetScan
    {
    public:
        struct FResult
        {
            // Unique virtual paths that resolved through FAssetRegistry.
            TVector<FString> AssetPaths;

            size_t FilesScanned    = 0;
            size_t RawCandidates   = 0;
            size_t ResolvedRefs    = 0;
        };

        static FResult ScanRoots(
            const TVector<FString>& VirtualRoots,
            const FAssetRegistry& Registry,
            const TFunction<void(FStringView)>& LogFunc = {});

        // Exposed for testing: extract candidate string-literal arguments
        // from `Asset.<Func>("…")` calls in a single buffer.
        static TVector<FString> ExtractCandidates(FStringView Contents);
    };
}
