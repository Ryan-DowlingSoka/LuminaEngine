#include "AssetCooker.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRegistry/AssetData.h"
#include "Config/Config.h"
#include "Core/Engine/Engine.h"
#include "Core/Object/Object.h"
#include "Core/Object/Package/Package.h"
#include "Core/Serialization/Package/PackageSaver.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Pak/PakWriter.h"
#include "World/World.h"

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
            LOG_INFO("[Cooker] {}", FString(Msg.data(), Msg.size()).c_str());
        }

        bool BundleVfsFile(FPakWriter& Writer, FStringView VirtualPath, const TFunction<void(FStringView)>& LogFunc)
        {
            TVector<uint8> Bytes;
            if (!VFS::ReadFile(Bytes, VirtualPath))
            {
                Log(LogFunc, FString().sprintf("  [skip] missing: %.*s", (int)VirtualPath.size(), VirtualPath.data()).c_str());
                return false;
            }

            const TSpan<const uint8> Span(Bytes.data(), Bytes.size());
            Writer.AddEntry(VirtualPath, Span);

            Log(LogFunc, FString().sprintf("  + %.*s (%zu bytes)",
                (int)VirtualPath.size(), VirtualPath.data(),
                Bytes.size()).c_str());
            return true;
        }

        // Walks the asset reference graph via each package's on-disk ImportTable.
        // Avoids FullyLoad/BuildSaveContext, which only see references on objects already realized in memory —
        // that path missed actor/component refs whenever the level wasn't open in the editor.
        void CollectAssetReferences(const FString& RootPackagePath, THashSet<FString>& OutPaths, const TFunction<void(FStringView)>& LogFunc)
        {
            TQueue<FString> Queue;
            Queue.push(RootPackagePath);

            while (!Queue.empty())
            {
                FString Path = Queue.front();
                Queue.pop();

                if (OutPaths.find(Path) != OutPaths.end())
                {
                    continue;
                }
                OutPaths.insert(Path);

                CPackage* Pkg = CPackage::LoadPackage(Path);
                if (Pkg == nullptr)
                {
                    Log(LogFunc, FString().sprintf("  [warn] could not load: %s", Path.c_str()).c_str());
                    continue;
                }

                for (const FObjectImport& Import : Pkg->ImportTable)
                {
                    const FAssetData* Data = FAssetRegistry::Get().GetAssetByGUID(Import.ObjectGUID);
                    if (Data == nullptr)
                    {
                        // Engine-resident objects (CDOs, classes, primitives) have no asset record.
                        continue;
                    }
                    FString DepPath(Data->Path.c_str());
                    if (OutPaths.find(DepPath) == OutPaths.end())
                    {
                        Queue.push(Move(DepPath));
                    }
                }
            }
        }

        // VirtualPath ends in ".lasset" (case-insensitive).
        bool IsLAssetPath(FStringView VirtualPath)
        {
            static constexpr FStringView Ext = ".lasset";
            if (VirtualPath.size() < Ext.size())
            {
                return false;
            }
            FStringView Tail = VirtualPath.substr(VirtualPath.size() - Ext.size());
            for (size_t i = 0; i < Ext.size(); ++i)
            {
                char a = Tail[i]; char b = Ext[i];
                if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
                if (a != b) return false;
            }
            return true;
        }

        // Bundle every non-.lasset file under /Game; .lasset files come in via the asset reference graph.
        // Whole-tree ship is fine since require()/href tracing across our language mix isn't feasible.
        size_t BundleGameLooseFiles(FPakWriter& Writer, const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Count = 0;
            VFS::RecursiveDirectoryIterator("/Game", [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory())
                {
                    return;
                }
                FStringView Vp(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (IsLAssetPath(Vp))
                {
                    return;
                }
                if (BundleVfsFile(Writer, Vp, LogFunc))
                {
                    ++Count;
                }
            });
            return Count;
        }

        bool BundleDiskFile(FPakWriter& Writer, const std::filesystem::path& DiskPath, FStringView VirtualPath, const TFunction<void(FStringView)>& LogFunc)
        {
            std::ifstream In(DiskPath, std::ios::binary | std::ios::ate);
            if (!In)
            {
                Log(LogFunc, FString().sprintf("  [skip] missing extra: %s", DiskPath.string().c_str()).c_str());
                return false;
            }
            const std::streamsize Size = In.tellg();
            In.seekg(0, std::ios::beg);
            TVector<uint8> Bytes((size_t)Size);
            if (Size > 0 && !In.read(reinterpret_cast<char*>(Bytes.data()), Size))
            {
                Log(LogFunc, FString().sprintf("  [skip] read failed: %s", DiskPath.string().c_str()).c_str());
                return false;
            }
            Writer.AddEntry(VirtualPath, TSpan<const uint8>(Bytes.data(), Bytes.size()));
            Log(LogFunc, FString().sprintf("  + %.*s (%zu bytes, extra)",
                (int)VirtualPath.size(), VirtualPath.data(), Bytes.size()).c_str());
            return true;
        }

        size_t BundleExtras(FPakWriter& Writer, const FCookOptions& Options, const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Count = 0;
            std::error_code Ec;

            for (const FString& File : Options.ExtraFiles)
            {
                std::filesystem::path P(File.c_str());
                if (!std::filesystem::exists(P, Ec) || !std::filesystem::is_regular_file(P, Ec))
                {
                    Log(LogFunc, FString().sprintf("  [skip] extra file not found: %s", File.c_str()).c_str());
                    continue;
                }
                FString Vp = FString("/Extras/") + P.filename().string().c_str();
                if (BundleDiskFile(Writer, P, FStringView(Vp.c_str(), Vp.size()), LogFunc))
                {
                    ++Count;
                }
            }

            for (const FString& Dir : Options.ExtraDirectories)
            {
                std::filesystem::path Root(Dir.c_str());
                if (!std::filesystem::exists(Root, Ec) || !std::filesystem::is_directory(Root, Ec))
                {
                    Log(LogFunc, FString().sprintf("  [skip] extra dir not found: %s", Dir.c_str()).c_str());
                    continue;
                }
                const std::string RootName = Root.filename().string();
                for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root, Ec))
                {
                    if (Ec || !Entry.is_regular_file())
                    {
                        continue;
                    }
                    std::filesystem::path Rel = std::filesystem::relative(Entry.path(), Root, Ec);
                    if (Ec)
                    {
                        continue;
                    }
                    std::string RelStr = Rel.generic_string();
                    FString Vp = FString("/Extras/") + RootName.c_str() + "/" + RelStr.c_str();
                    if (BundleDiskFile(Writer, Entry.path(), FStringView(Vp.c_str(), Vp.size()), LogFunc))
                    {
                        ++Count;
                    }
                }
            }
            return Count;
        }

        // Inject Project.Name into /Config/GameSettings.json so the cooked runtime can resolve the project DLL.
        bool BundleConfigWithProjectName(FPakWriter& Writer, const TFunction<void(FStringView)>& LogFunc)
        {
            FString JsonText;
            if (!VFS::ReadFile(JsonText, "/Config/GameSettings.json"))
            {
                JsonText = "{}";
            }

            nlohmann::json Doc;
            try
            {
                Doc = nlohmann::json::parse(JsonText.c_str());
            }
            catch (...)
            {
                Doc = nlohmann::json::object();
            }

            if (!Doc.contains("Project") || !Doc["Project"].is_object())
            {
                Doc["Project"] = nlohmann::json::object();
            }
            Doc["Project"]["Name"] = std::string(GEngine->GetProjectName().data(), GEngine->GetProjectName().size());

            const std::string Out = Doc.dump(4);
            Writer.AddEntry("/Config/GameSettings.json", FStringView(Out.c_str(), Out.size()));
            Log(LogFunc, FString().sprintf("  + /Config/GameSettings.json (cooked, %zu bytes)", Out.size()).c_str());
            return true;
        }

        // True if Path lies under /Engine/Resources/Shaders. Slang sources are
        // intentionally excluded from the pak — packaged builds load SPIR-V
        // from the shader cache instead.
        bool IsShaderSourcePath(FStringView Path)
        {
            constexpr FStringView Prefix = "/Engine/Resources/Shaders";
            if (Path.size() < Prefix.size())
            {
                return false;
            }
            for (size_t i = 0; i < Prefix.size(); ++i)
            {
                if (Path[i] != Prefix[i])
                {
                    return false;
                }
            }
            return Path.size() == Prefix.size() || Path[Prefix.size()] == '/';
        }

        // Bundle runtime-needed engine content: /Engine/Resources/{Content,Fonts,Textures,UI,...}.
        // Skips Shaders (covered by BundleShaderCache).
        size_t BundleEngineResources(FPakWriter& Writer, const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Count = 0;
            VFS::RecursiveDirectoryIterator("/Engine/Resources", [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory())
                {
                    return;
                }
                FStringView Vp(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (IsShaderSourcePath(Vp))
                {
                    return;
                }
                if (BundleVfsFile(Writer, Vp, LogFunc))
                {
                    ++Count;
                }
            });
            return Count;
        }

        // Bundle every cached SPIR-V (.lsc) under /Intermediates/ShaderCache.
        // Packaged builds load these directly instead of running Slang.
        size_t BundleShaderCache(FPakWriter& Writer, const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Count = 0;
            VFS::RecursiveDirectoryIterator("/Intermediates/ShaderCache", [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory() || Info.GetExt() != ".lsc")
                {
                    return;
                }
                if (BundleVfsFile(Writer, Info.VirtualPath.c_str(), LogFunc))
                {
                    ++Count;
                }
            });
            return Count;
        }

        // Bundle other /Config/*.json files; GameSettings.json is handled by BundleConfigWithProjectName.
        size_t BundleAuxConfigFiles(FPakWriter& Writer, const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Count = 0;
            VFS::DirectoryIterator("/Config", [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory() || Info.GetExt() != ".json")
                {
                    return;
                }
                FStringView Vp(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (Vp == FStringView("/Config/GameSettings.json"))
                {
                    return;
                }
                if (BundleVfsFile(Writer, Vp, LogFunc))
                {
                    ++Count;
                }
            });
            return Count;
        }
    }

    FCookResult FAssetCooker::Cook(FStringView OutputPakPath, const FCookOptions& Options, const TFunction<void(FStringView)>& LogFunc)
    {
        FCookResult Result;

        if (GEngine == nullptr || GEngine->GetProjectName().empty())
        {
            Result.ErrorMessage = "No project loaded.";
            return Result;
        }

        const FString RawStartupMap = GConfig->Get<std::string>("Project.GameStartupMap").c_str();
        if (RawStartupMap.empty())
        {
            Result.ErrorMessage = "Project.GameStartupMap is not set.";
            return Result;
        }

        // Accept VFS paths or legacy absolute paths (older configs predate the resolver).
        const FFixedString ResolvedFixed = VFS::ResolveToVirtualPath(RawStartupMap);
        const FString ResolvedStartupMap(ResolvedFixed.c_str(), ResolvedFixed.size());

        const FAssetData* StartupAsset = FAssetRegistry::Get().GetAssetByPath(ResolvedStartupMap);
        if (StartupAsset == nullptr)
        {
            Result.ErrorMessage = FString().sprintf(
                "Startup map not found in asset registry.\n"
                "  Configured: %s\n"
                "  Resolved to: %s\n"
                "  Hint: re-pick it in Project Settings -> Project / Maps; the path must live under a mounted alias (/Game/Content/...).",
                RawStartupMap.c_str(), ResolvedStartupMap.c_str()).c_str();
            return Result;
        }

        Log(LogFunc, FString().sprintf("Cooking from startup map: %s", StartupAsset->Path.c_str()).c_str());

        THashSet<FString> Reachable;
        CollectAssetReferences(FString(StartupAsset->Path.c_str()), Reachable, LogFunc);

        Log(LogFunc, FString().sprintf("Reachable assets: %zu", Reachable.size()).c_str());

        FPakWriter Writer;
        for (const FString& Path : Reachable)
        {
            if (BundleVfsFile(Writer, Path, LogFunc))
            {
                ++Result.NumAssetsCooked;
            }
        }

        if (BundleConfigWithProjectName(Writer, LogFunc))
        {
            ++Result.NumExtraFiles;
        }
        Result.NumExtraFiles += BundleAuxConfigFiles(Writer, LogFunc);

        Log(LogFunc, "Bundling engine resources...");
        const size_t NumEngine = BundleEngineResources(Writer, LogFunc);
        Result.NumExtraFiles += NumEngine;
        Log(LogFunc, FString().sprintf("  bundled %zu engine files", NumEngine).c_str());

        Log(LogFunc, "Bundling shader cache...");
        const size_t NumShaders = BundleShaderCache(Writer, LogFunc);
        Result.NumExtraFiles += NumShaders;
        Log(LogFunc, FString().sprintf("  bundled %zu cached shaders", NumShaders).c_str());

        // Loose /Game files (.luau, .rml, JSON, etc). Skipped in loose-files mode; .lasset files come via the asset graph.
        if (!Options.bExtractScriptsAsLooseFiles)
        {
            Result.NumExtraFiles += BundleGameLooseFiles(Writer, LogFunc);
        }
        else
        {
            Log(LogFunc, "Skipping loose /Game files in PAK (loose mode).");
        }

        if (!Options.ExtraFiles.empty() || !Options.ExtraDirectories.empty())
        {
            Log(LogFunc, "Bundling extras...");
            Result.NumExtraFiles += BundleExtras(Writer, Options, LogFunc);
        }

        Result.TotalBytes = Writer.TotalEntryBytes();
        if (!Writer.Finalize(OutputPakPath))
        {
            Result.ErrorMessage = FString("Failed to write PAK at ") + FString(OutputPakPath.data(), OutputPakPath.size());
            return Result;
        }

        Log(LogFunc, FString().sprintf("Wrote PAK: %.*s (%zu entries, %zu bytes)",
            (int)OutputPakPath.size(), OutputPakPath.data(),
            Writer.NumEntries(), Result.TotalBytes).c_str());

        Result.bSuccess = true;
        return Result;
    }
}
