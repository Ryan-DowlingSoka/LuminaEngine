#pragma once

#include "Containers/Function.h"
#include "Containers/String.h"

namespace Lumina
{
    struct FPackageBuildResult
    {
        bool    bSuccess        = false;
        FString OutputDirectory;        // Where the final exe + .pak ended up
        FString PakPath;
        FString ErrorMessage;
    };

    struct FPackageBuildOptions
    {
        // Where to drop the final cooked build. Default: <ProjectDir>/Build/<ProjectName>/
        FString OutputDirectory;

        // If true, runs MSBuild for Game|Shipping before copying outputs.
        // If false, only the cook step runs and outputs go into a /Cooked/ subfolder.
        bool    bBuildExecutable = true;

        // Ship Lua scripts as loose, editable files in <output>/Game/Scripts/
        // rather than bundling them in the PAK. The cooked runtime mounts that
        // folder as a /Game overlay so loads find them transparently.
        bool    bExtractScriptsAsLooseFiles = false;

        // Configuration to pass to MSBuild ("Shipping" recommended; "Development" for debugging).
        FString MSBuildConfiguration = "Shipping";

        // Absolute path to MSBuild.exe. Empty = use the saved default.
        FString MSBuildPath;
    };

    /**
     * High-level orchestrator for "package this project for shipping":
     *   1. Cook the asset graph into <Out>/<ProjectName>.pak
     *   2. Optionally invoke MSBuild for Game|<Config>
     *   3. Copy Lumina-<Config>.exe, Runtime-<Config>.dll, and the project DLL
     *      next to the .pak so the user can ship the folder as-is.
     *
     * MSBuild runs as a hidden subprocess; its stdout+stderr are streamed
     * through LogFunc one line at a time so the editor can render real-time
     * output. The whole call is synchronous — for an async run, drive the
     * two stages from the caller (see BuildAndCopyOnly).
     */
    class FProjectPackager
    {
    public:

        // Runs cook → build → copy in one synchronous call.
        static FPackageBuildResult Package(const FPackageBuildOptions& Options, const TFunction<void(FStringView)>& LogFunc = {});

        /**
         * Just the MSBuild + binary-copy stages. Expects a .pak to already exist
         * at <Options.OutputDirectory>/<ProjectName>.pak (typically because
         * the caller ran FAssetCooker::Cook on the main thread first).
         *
         * Safe to call from a worker thread — touches no engine state.
         * LogFunc may be invoked from that worker thread; the caller is
         * responsible for synchronizing access to whatever it pushes into.
         */
        static FPackageBuildResult BuildAndCopyOnly(
            const FPackageBuildOptions& Options,
            FStringView ProjectName,
            FStringView PakPath,
            const TFunction<void(FStringView)>& LogFunc = {});

        // Mirrors /Game/Scripts/ in the VFS as loose files under
        // <OutDir>/Game/Scripts/. Returns the file count copied. Must be
        // called on the main thread because it walks the VFS, which the
        // editor's other systems may mutate concurrently otherwise.
        static size_t ExtractLooseScripts(const FString& OutDir, const TFunction<void(FStringView)>& LogFunc = {});

        // Convenience: returns the default MSBuild path baked into the editor
        // ("C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe").
        // The user can override via FPackageBuildOptions::MSBuildPath.
        static FString DefaultMSBuildPath();
    };
}
