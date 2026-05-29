#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"


namespace Lumina
{
    class FAssetRegistry;

    /** Scans .luau scripts under the given VFS roots for the Lua-side asset
     *  API call sites (`Asset.Hard("…")`, `Asset.Soft("…")`, `Asset.LoadAsync("…")`)
     *  plus absolute-path `require("/…")` calls, and returns the set of
     *  discovered virtual paths that resolve to a registered FAssetData.
     *
     *  Walks `require()` chains transitively: a script discovered via
     *  require() from the seed roots is itself analyzed for further
     *  references. Lets engine stdlib (`/Engine/Resources/Content/Scripts`)
     *  enter the analysis once any /Game-rooted script requires it.
     *
     *  These are folded into the cook graph as implicit Script cook roots
     *  (treated like Soft at cook time) so assets reachable only through
     *  script lookup are pulled into the PAK.
     *
     *  Limitations: pattern-based, not a real parser. Only string-literal
     *  arguments are detected; dynamic paths (`Asset.Hard(some_var)`) and
     *  Luau-relative requires (`require("./Foo")`, `require("Stdlib.Foo")`)
     *  aren't. Scripts that need dynamic loads should expose their candidate
     *  set via a data asset that's a real cook root. */
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
