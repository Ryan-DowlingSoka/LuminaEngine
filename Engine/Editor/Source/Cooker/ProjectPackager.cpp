#include "ProjectPackager.h"

#include <filesystem>

#include "AssetCooker.h"
#include "Core/Engine/Engine.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Paths/Paths.h"
#include "Platform/Process/PlatformProcess.h"

#include <fstream>

namespace Lumina
{
    namespace
    {
        void Log(const TFunction<void(FStringView)>& LogFunc, FStringView Msg)
        {
            if (LogFunc)
            {
                LogFunc(Msg);
            }
            LOG_INFO("[Packager] {}", FString(Msg.data(), Msg.size()).c_str());
        }

        // Copies one file with overwrite. Returns false on filesystem error.
        bool CopyFileTo(const std::filesystem::path& Src, const std::filesystem::path& Dst)
        {
            std::error_code Ec;
            std::filesystem::create_directories(Dst.parent_path(), Ec);
            std::filesystem::copy_file(Src, Dst, std::filesystem::copy_options::overwrite_existing, Ec);
            return !Ec;
        }

        // Pulls every file under /Game/Scripts/ in the VFS and writes a loose
        // mirror under <OutDir>/Game/Scripts/. Returns the number of files
        // copied. Used when bExtractScriptsAsLooseFiles is set so users can
        // tweak script behavior in the shipped build without re-cooking.
        size_t CopyLooseScripts(const FString& OutDir, const TFunction<void(FStringView)>& LogFunc)
        {
            const std::filesystem::path ScriptsRoot =
                std::filesystem::path(OutDir.c_str()) / "Game" / "Scripts";

            std::error_code Ec;
            std::filesystem::create_directories(ScriptsRoot, Ec);

            size_t Count = 0;
            VFS::RecursiveDirectoryIterator("/Game/Scripts", [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory())
                {
                    return;
                }

                TVector<uint8> Bytes;
                if (!VFS::ReadFile(Bytes, Info.VirtualPath))
                {
                    return;
                }

                // Mirror the relative directory structure.
                FStringView Vp(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                static constexpr FStringView Prefix = "/Game/Scripts/";
                if (Vp.size() <= Prefix.size())
                {
                    return;
                }
                FStringView Relative = Vp.substr(Prefix.size());

                std::filesystem::path Dst = ScriptsRoot / std::string(Relative.data(), Relative.size());
                std::filesystem::create_directories(Dst.parent_path(), Ec);

                std::ofstream Out(Dst, std::ios::binary);
                if (!Out)
                {
                    return;
                }
                Out.write(reinterpret_cast<const char*>(Bytes.data()), (std::streamsize)Bytes.size());

                ++Count;
                if (LogFunc)
                {
                    LogFunc(FString().sprintf("  + Game/Scripts/%.*s",
                        (int)Relative.size(), Relative.data()).c_str());
                }
            });
            return Count;
        }

        // True if Stem ends with "-<OtherConfig>" — used to skip wrong-config
        // DLLs left over from a previous Editor build (e.g. Runtime-Development.dll
        // sitting next to Runtime-Shipping.dll).
        bool HasOtherConfigSuffix(const std::string& Stem, const FString& MyConfig)
        {
            const char* Configs[] = { "Debug", "Development", "Shipping" };
            for (const char* Cfg : Configs)
            {
                if (MyConfig == Cfg)
                {
                    continue;
                }
                const std::string Suffix = std::string("-") + Cfg;
                if (Stem.size() >= Suffix.size() &&
                    Stem.compare(Stem.size() - Suffix.size(), Suffix.size(), Suffix) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // Copies the runtime payload from the build's flat Binaries/Windows64/
        // folder into the cooked output directory. We're permissive on
        // purpose: third-party DLLs land here under various naming schemes
        // (slang.dll has no config suffix; Tracy-Shipping.dll has one), and
        // the cooked exe needs all of them. We skip:
        //   - Other-config siblings (Runtime-Development.dll when shipping)
        //   - The Lumina executable for configs other than ours
        //   - Editor-only DLLs (Editor-*.dll), tools (Reflector.exe, Tests-*.exe)
        //   - Linker artifacts (.lib, .exp, .pdb)
        // Returns true if any DLL in SourceDir has the same base name as Stem
        // but with a config suffix (e.g. Stem="Luau" matches "Luau-Shipping.dll").
        // Used to detect stale pre-targetsuffix copies — when both Luau.dll and
        // Luau-Shipping.dll exist, the unsuffixed one is dead weight.
        bool HasSuffixedSibling(const std::filesystem::path& SourceDir, const std::string& Stem)
        {
            std::error_code Ec;
            for (const char* Cfg : { "Debug", "Development", "Shipping" })
            {
                std::filesystem::path Sibling = SourceDir / (Stem + "-" + Cfg + ".dll");
                if (std::filesystem::exists(Sibling, Ec))
                {
                    return true;
                }
            }
            return false;
        }

        // Editor-only / tooling DLLs that get dropped into Binaries/Windows64/
        // by various build steps but aren't needed at runtime. Hard-coded
        // because there's no programmatic way to tell them apart from real
        // runtime deps like slang.dll.
        bool IsEditorOnlyDll(const std::string& FileName)
        {
            // libclang.dll is shipped for the Reflector tool's Clang frontend.
            // Pure compile-time dependency.
            return FileName == "libclang.dll";
        }

        size_t CopyRuntimePayload(const std::filesystem::path& SourceDir,
                                  const std::filesystem::path& DestDir,
                                  const FString& ConfigSuffix,
                                  const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Copied = 0;
            size_t Skipped = 0;
            const std::string MyExeName = std::string("Lumina-") + ConfigSuffix.c_str() + ".exe";

            std::error_code Ec;
            for (const auto& Entry : std::filesystem::directory_iterator(SourceDir, Ec))
            {
                if (!Entry.is_regular_file())
                {
                    continue;
                }

                const std::string FileName = Entry.path().filename().string();
                const std::string Ext      = Entry.path().extension().string();
                const std::string Stem     = Entry.path().stem().string();

                // Tools / test exes / wrong-config exes — never ship.
                if (Ext == ".exe" && FileName != MyExeName)
                {
                    continue;
                }

                // Runtime payload only — DLLs and the matching exe.
                if (Ext != ".dll" && Ext != ".exe")
                {
                    continue;
                }

                // Skip wrong-config DLLs left from a previous build.
                if (HasOtherConfigSuffix(Stem, ConfigSuffix))
                {
                    ++Skipped;
                    continue;
                }

                // Editor-only DLLs aren't needed by the cooked game and won't
                // be present anyway when the editor is properly removed from
                // the Game platform — but if a prior Editor build left one
                // behind, skip it explicitly.
                if (Stem.size() >= 7 && Stem.compare(0, 7, "Editor-") == 0)
                {
                    ++Skipped;
                    continue;
                }

                // Editor-only tooling DLLs (libclang for the Reflector etc.).
                if (IsEditorOnlyDll(FileName))
                {
                    ++Skipped;
                    continue;
                }

                // Stale unsuffixed copy of a now-suffixed DLL. Pre-targetsuffix
                // builds dumped Luau.dll / Tracy.dll into Binaries/; those linger
                // alongside the proper Luau-Shipping.dll and would just waste space.
                if (Ext == ".dll"
                    && Stem.find("-Debug") == std::string::npos
                    && Stem.find("-Development") == std::string::npos
                    && Stem.find("-Shipping") == std::string::npos
                    && HasSuffixedSibling(SourceDir, Stem))
                {
                    ++Skipped;
                    continue;
                }

                std::filesystem::path Dst = DestDir / Entry.path().filename();
                if (CopyFileTo(Entry.path(), Dst))
                {
                    ++Copied;
                    Log(LogFunc, FString().sprintf("  + %s", FileName.c_str()).c_str());
                }
                else
                {
                    Log(LogFunc, FString().sprintf("  [warn] failed to copy %s", FileName.c_str()).c_str());
                }
            }

            if (Skipped > 0)
            {
                Log(LogFunc, FString().sprintf("  (skipped %zu wrong-config / editor-only / stale files)", Skipped).c_str());
            }
            return Copied;
        }
    }

    FPackageBuildResult FProjectPackager::BuildAndCopyOnly(
        const FPackageBuildOptions& Options,
        FStringView ProjectName,
        FStringView PakPath,
        const TFunction<void(FStringView)>& LogFunc)
    {
        FPackageBuildResult Result;
        Result.OutputDirectory = Options.OutputDirectory;
        Result.PakPath.assign(PakPath.data(), PakPath.size());

        const FString MSBuild = Options.MSBuildPath.empty() ? DefaultMSBuildPath() : Options.MSBuildPath;
        const FString Config  = Options.MSBuildConfiguration.empty() ? FString("Shipping") : Options.MSBuildConfiguration;

        const FString SolutionPath = FString(Paths::GetEngineInstallDirectory().c_str()) + "/Lumina.sln";

        FString Args;
        Args = FString("\"") + SolutionPath + "\""
            + " -p:Configuration=" + Config
            + " -p:Platform=Game"
            + " -m -v:minimal -nologo";

        Log(LogFunc, FString().sprintf("Running MSBuild: %s %s", MSBuild.c_str(), Args.c_str()).c_str());

        const std::wstring MSBuildW(MSBuild.begin(), MSBuild.end());
        const std::wstring ArgsW(Args.begin(), Args.end());
        const std::wstring CwdW(Paths::GetEngineInstallDirectory().c_str(),
            Paths::GetEngineInstallDirectory().c_str() + Paths::GetEngineInstallDirectory().size());

        // Stream every MSBuild line through LogFunc so the editor can render
        // realtime build output. The callback fires on whichever thread is
        // running this function; the caller's LogFunc must handle that
        // (e.g. a mutex-protected push into a UI-thread-drained queue).
        const int ExitCode = Platform::RunProcessAndWaitCapture(
            MSBuildW.c_str(), ArgsW.c_str(), CwdW.c_str(),
            [&LogFunc](FStringView Line)
            {
                if (LogFunc && !Line.empty())
                {
                    FString Prefixed = FString("  | ");
                    Prefixed.append(Line.data(), Line.size());
                    LogFunc(Prefixed);
                }
            });

        if (ExitCode != 0)
        {
            Result.ErrorMessage = FString().sprintf(
                "MSBuild failed (exit code %d). See log above for the build error. Cooked PAK is still at %.*s.",
                ExitCode, (int)PakPath.size(), PakPath.data());
            return Result;
        }

        // Copy <Config>-suffixed exe + dlls into the output folder.
        const std::filesystem::path BinariesDir =
            std::filesystem::path(Paths::GetEngineInstallDirectory().c_str()) / "Binaries" / "Windows64";
        const std::filesystem::path DestDir(Options.OutputDirectory.c_str());

        Log(LogFunc, FString().sprintf("Copying %s binaries from %s",
            Config.c_str(), BinariesDir.string().c_str()).c_str());

        const size_t Copied = CopyRuntimePayload(BinariesDir, DestDir, Config, LogFunc);
        if (Copied == 0)
        {
            Result.ErrorMessage = "MSBuild reported success but no matching binaries were found to copy. Check the build output.";
            return Result;
        }
        Log(LogFunc, FString().sprintf("Copied %zu runtime files.", Copied).c_str());

        Result.bSuccess = true;
        return Result;
    }

    size_t FProjectPackager::ExtractLooseScripts(const FString& OutDir, const TFunction<void(FStringView)>& LogFunc)
    {
        return CopyLooseScripts(OutDir, LogFunc);
    }

    FString FProjectPackager::DefaultMSBuildPath()
    {
        // Cache the result — vswhere subprocess invocation isn't free, and
        // the answer never changes within a session.
        static FString CachedPath;
        if (!CachedPath.empty())
        {
            return CachedPath;
        }

        // vswhere.exe ships at a well-known location with every VS 2017+
        // install (including the standalone Build Tools). It's the canonical
        // Microsoft recipe for finding MSBuild and works regardless of the
        // user's edition (Community/Pro/Enterprise/BuildTools).
        //
        // The -find pattern matches both x86 (MSBuild\Current\Bin\MSBuild.exe)
        // and amd64 flavors; we take the first line, which is the x86 build —
        // either bitness builds our solution fine.
        const char* VsWhereCandidates[] =
        {
            R"(C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe)",
            R"(C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe)",
        };

        for (const char* Candidate : VsWhereCandidates)
        {
            std::error_code Ec;
            if (!std::filesystem::exists(Candidate, Ec))
            {
                continue;
            }

            FString FirstLine;
            // vswhere.exe paths are ASCII-only; iterator-construct a wide
            // string the same way the BuildAndCopyOnly path does.
            const std::string CandidateStr(Candidate);
            const std::wstring VsWhereW(CandidateStr.begin(), CandidateStr.end());
            const std::wstring ArgsW = LR"(-latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" -nologo)";

            const int ExitCode = Platform::RunProcessAndWaitCapture(
                VsWhereW.c_str(), ArgsW.c_str(), nullptr,
                [&FirstLine](FStringView Line)
                {
                    // vswhere can emit multiple matches (x86 + amd64); we
                    // only need the first usable one.
                    if (FirstLine.empty() && !Line.empty())
                    {
                        FirstLine.assign(Line.data(), Line.size());
                    }
                });

            if (ExitCode == 0 && !FirstLine.empty())
            {
                // vswhere returns Windows-style backslashes; normalize so the
                // path round-trips through the rest of the engine cleanly.
                std::replace(FirstLine.begin(), FirstLine.end(), '\\', '/');
                CachedPath = Move(FirstLine);
                return CachedPath;
            }
        }

        // Last-resort fallback: the most common VS 2022 Community install
        // path. If this is also wrong, the user can override via the editor
        // tool's "MSBuild Path" field.
        CachedPath = "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe";
        return CachedPath;
    }

    FPackageBuildResult FProjectPackager::Package(const FPackageBuildOptions& Options, const TFunction<void(FStringView)>& LogFunc)
    {
        FPackageBuildResult Result;

        if (GEngine == nullptr || GEngine->GetProjectName().empty())
        {
            Result.ErrorMessage = "No project loaded.";
            return Result;
        }

        const FString ProjectName(GEngine->GetProjectName().data(), GEngine->GetProjectName().size());

        // 1) Resolve output directory. Default: <ProjectDir>/Build/<ProjectName>/.
        FString OutDir = Options.OutputDirectory;
        if (OutDir.empty())
        {
            OutDir = FString(GEngine->GetProjectPath().data(), GEngine->GetProjectPath().size())
                + "/Build/" + ProjectName;
        }

        std::error_code Ec;
        std::filesystem::create_directories(OutDir.c_str(), Ec);
        if (Ec)
        {
            Result.ErrorMessage = FString("Failed to create output directory: ") + OutDir + " (" + Ec.message().c_str() + ")";
            return Result;
        }
        Result.OutputDirectory = OutDir;

        Log(LogFunc, FString().sprintf("Output directory: %s", OutDir.c_str()).c_str());

        // 2) Cook → .pak
        const FString PakPath = OutDir + "/" + ProjectName + ".pak";
        Log(LogFunc, FString().sprintf("Cooking PAK: %s", PakPath.c_str()).c_str());

        FCookOptions CookOpts;
        CookOpts.bExtractScriptsAsLooseFiles = Options.bExtractScriptsAsLooseFiles;

        const FCookResult Cook = FAssetCooker::Cook(PakPath, CookOpts, LogFunc);
        if (!Cook.bSuccess)
        {
            Result.ErrorMessage = FString("Cook failed: ") + Cook.ErrorMessage;
            return Result;
        }
        Result.PakPath = PakPath;
        Log(LogFunc, FString().sprintf("Cook OK: %zu assets, %zu extras, %zu bytes",
            Cook.NumAssetsCooked, Cook.NumExtraFiles, Cook.TotalBytes).c_str());

        // 2.5) Extract loose scripts mirror, if requested.
        if (Options.bExtractScriptsAsLooseFiles)
        {
            Log(LogFunc, "Extracting loose scripts under Game/Scripts/...");
            const size_t Extracted = CopyLooseScripts(OutDir, LogFunc);
            Log(LogFunc, FString().sprintf("Extracted %zu loose script files.", Extracted).c_str());
        }

        // 3) MSBuild + binary copy. Delegated to the worker-thread-safe variant.
        if (Options.bBuildExecutable)
        {
            FPackageBuildOptions LocalOpts = Options;
            LocalOpts.OutputDirectory = OutDir;
            const FPackageBuildResult Sub = BuildAndCopyOnly(LocalOpts, ProjectName, PakPath, LogFunc);
            if (!Sub.bSuccess)
            {
                Result.ErrorMessage = Sub.ErrorMessage;
                return Result;
            }
        }
        else
        {
            Log(LogFunc, "Skipped executable build (cook only).");
        }

        Result.bSuccess = true;
        Log(LogFunc, "Package complete.");
        return Result;
    }
}
