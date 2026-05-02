#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/String.h"

namespace Lumina
{
    struct FPackageBuildResult
    {
        bool    bSuccess        = false;
        FString OutputDirectory;
        FString PakPath;
        FString ErrorMessage;
    };

    struct FPackageBuildOptions
    {
        // Default: <ProjectDir>/Build/<ProjectName>/
        FString OutputDirectory;

        // True runs MSBuild Game|<Config> before copying; false stops after cook.
        bool    bBuildExecutable = true;

        // Mirror loose /Game scripts next to the exe instead of in the PAK.
        bool    bExtractScriptsAsLooseFiles = false;

        // MSBuild configuration ("Shipping" recommended; "Development" for debugging).
        FString MSBuildConfiguration = "Shipping";

        // Absolute path to MSBuild.exe; empty uses DefaultMSBuildPath().
        FString MSBuildPath;

        // Embedded under /Extras/ in the PAK.
        TVector<FString> ExtraFiles;
        TVector<FString> ExtraDirectories;
    };

    /** Cook + (optional) MSBuild + binary copy for shipping a project.
     *  MSBuild runs as a hidden subprocess; stdout/stderr stream through LogFunc. Synchronous. */
    class FProjectPackager
    {
    public:

        static FPackageBuildResult Package(const FPackageBuildOptions& Options, const TFunction<void(FStringView)>& LogFunc = {});

        /** MSBuild + binary-copy stages only; expects a .pak to already exist at <OutputDirectory>/<ProjectName>.pak.
         *  Safe from a worker thread (touches no engine state); LogFunc may be invoked from that thread. */
        static FPackageBuildResult BuildAndCopyOnly(
            const FPackageBuildOptions& Options,
            FStringView ProjectName,
            FStringView PakPath,
            const TFunction<void(FStringView)>& LogFunc = {});

        // Mirrors /Game/Scripts/ as loose files under <OutDir>/Game/Scripts/. Main-thread only (walks VFS).
        static size_t ExtractLooseScripts(const FString& OutDir, const TFunction<void(FStringView)>& LogFunc = {});

        // Default MSBuild path baked into the editor; overridable via FPackageBuildOptions::MSBuildPath.
        static FString DefaultMSBuildPath();
    };
}
