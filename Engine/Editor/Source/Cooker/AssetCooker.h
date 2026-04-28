#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"

namespace Lumina
{
    struct FCookOptions
    {
        // When true, every non-.lasset file under /Game/ is NOT bundled into
        // the PAK. The packager copies them as loose files next to the cooked
        // exe instead, and the cooked runtime mounts that folder so loads find
        // them. Useful for shipping moddable / tweakable game logic and UI
        // without requiring users to re-cook.
        bool bExtractScriptsAsLooseFiles = false;

        // Absolute paths to additional files to embed in the PAK. Each file
        // lands at /Extras/<filename> in the cooked VFS.
        TVector<FString> ExtraFiles;

        // Absolute paths to additional directories to embed in the PAK,
        // walked recursively. Files land at /Extras/<dirname>/<relative path>.
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

    /**
     * Walks the asset graph rooted at Project.GameStartupMap, then writes
     * every transitively-referenced .lasset, the project config, and the
     * project's Lua scripts into a single .pak file. Read by the cooked
     * runtime via FEngine::LoadCookedRuntime.
     *
     * The walk uses CPackage::BuildSaveContext to discover cross-package
     * imports — same machinery package save uses to emit the import table —
     * so any TObjectPtr reference the engine can serialize gets cooked.
     */
    class FAssetCooker
    {
    public:

        // LogFunc receives a one-line status message per stage / per file.
        // Pass {} to suppress.
        static FCookResult Cook(FStringView OutputPakPath, const FCookOptions& Options = {}, const TFunction<void(FStringView)>& LogFunc = {});
    };
}
