#include "AssetCooker.h"
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

        // Walks the asset reference graph starting from RootPackagePath. Uses
        // CPackage::BuildSaveContext so we get the same dependency set the
        // package would emit as imports when saved.
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

                if (!Pkg->FullyLoad())
                {
                    Log(LogFunc, FString().sprintf("  [warn] could not fully load: %s", Path.c_str()).c_str());
                    continue;
                }

                FSaveContext Context(Pkg);
                Pkg->BuildSaveContext(Context);

                for (CObject* Import : Context.Imports)
                {
                    if (Import == nullptr)
                    {
                        continue;
                    }
                    const FAssetData* Data = FAssetRegistry::Get().GetAssetByGUID(Import->GetGUID());
                    if (Data == nullptr)
                    {
                        // Engine-resident object (CDOs, classes, primitives)
                        // have no asset record — these don't need cooking.
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

        // Bundle the Lua scripts under /Game/Scripts/. The Luau loader walks
        // require()s at runtime; rather than tracing them (no deps machinery
        // for Lua) we just ship the whole tree. Tens-of-KB; cheap.
        size_t BundleScripts(FPakWriter& Writer, const TFunction<void(FStringView)>& LogFunc)
        {
            size_t Count = 0;
            VFS::RecursiveDirectoryIterator("/Game/Scripts", [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory())
                {
                    return;
                }
                if (BundleVfsFile(Writer, Info.VirtualPath, LogFunc))
                {
                    ++Count;
                }
            });
            return Count;
        }

        // Reads /Config/GameSettings.json, injects "Project.Name" so the
        // cooked runtime can resolve the project DLL filename, and bundles
        // the modified JSON. The on-disk file is left untouched.
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

        // Accept either VFS paths or legacy absolute Windows paths from the
        // file dialog (config files predating the path-resolver still hold those).
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

        // 1) Walk the asset reference graph.
        THashSet<FString> Reachable;
        CollectAssetReferences(FString(StartupAsset->Path.c_str()), Reachable, LogFunc);

        Log(LogFunc, FString().sprintf("Reachable assets: %zu", Reachable.size()).c_str());

        // 2) Pack each reachable asset into the PAK.
        FPakWriter Writer;
        for (const FString& Path : Reachable)
        {
            if (BundleVfsFile(Writer, Path, LogFunc))
            {
                ++Result.NumAssetsCooked;
            }
        }

        // 3) Project config (with injected Project.Name).
        if (BundleConfigWithProjectName(Writer, LogFunc))
        {
            ++Result.NumExtraFiles;
        }

        // 4) Lua scripts. Skipped when the caller wants them shipped as
        //    loose files (the packager copies them next to the exe instead).
        if (!Options.bExtractScriptsAsLooseFiles)
        {
            Result.NumExtraFiles += BundleScripts(Writer, LogFunc);
        }
        else
        {
            Log(LogFunc, "Skipping /Game/Scripts in PAK (loose-script mode).");
        }

        // 5) Write the .pak.
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
