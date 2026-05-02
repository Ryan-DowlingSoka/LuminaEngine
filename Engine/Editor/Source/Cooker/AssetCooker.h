#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"

namespace Lumina
{
    struct FCookOptions
    {
        // Ship loose /Game files next to the exe instead of in the PAK (mountable as overlay for tweakability).
        bool bExtractScriptsAsLooseFiles = false;

        // Absolute paths embedded at /Extras/<filename> in the cooked VFS.
        TVector<FString> ExtraFiles;

        // Absolute directories walked recursively; files land at /Extras/<dirname>/<relative>.
        TVector<FString> ExtraDirectories;
    };

    struct FCookResult
    {
        bool    bSuccess        = false;
        size_t  NumAssetsCooked = 0;
        size_t  NumExtraFiles   = 0;
        size_t  TotalBytes      = 0;
        FString ErrorMessage;
    };

    /** Walks the asset graph from Project.GameStartupMap and writes every transitively-referenced .lasset,
     *  the project config, and Lua scripts into a single .pak. Read by the cooked runtime via FEngine::LoadCookedRuntime. */
    class FAssetCooker
    {
    public:

        // LogFunc gets one line per stage/file; {} to suppress.
        static FCookResult Cook(FStringView OutputPakPath, const FCookOptions& Options = {}, const TFunction<void(FStringView)>& LogFunc = {});
    };
}
