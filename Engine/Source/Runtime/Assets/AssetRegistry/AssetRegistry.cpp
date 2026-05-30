#include "pch.h"
#include "AssetRegistry.h"

#include "Core/Math/Hash/Hash.h"
#include "Core/Object/Package/Package.h"
#include "Core/Plugin/Plugin.h"
#include "Core/Plugin/PluginManager.h"
#include "Core/Serialization/Archiver.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "TaskSystem/TaskSystem.h"
#include "TaskSystem/ThreadedCallback.h"
#include "Tools/UI/ImGui/ImGuiX.h"

#include <chrono>
#include <filesystem>

namespace Lumina
{
    // Tag both the on-disk .assetdb cache AND the cooked-runtime registry
    // blob bundled into the PAK. Same wire format both directions.
    static constexpr uint32 kAssetRegistryCacheTag     = 0xA55E1DB1; // 'AssetIDB1'

    FAssetRegistry& FAssetRegistry::Get()
    {
        static FAssetRegistry Registry;
        return Registry;
    }

    namespace
    {
        FString AssetDbPath()
        {
            const FString& Install = Paths::GetEngineInstallDirectory();
            if (Install.empty()) return {};
            FString Out = Install;
            Out += "/Intermediates/AssetRegistry.assetdb";
            return Out;
        }

        int64 FileMTimeNanos(FStringView VirtualPath)
        {
            // Resolve virtual -> absolute and stat via std::filesystem (VFS has no uniform mtime accessor).
            const FFixedString Resolved = VFS::ResolvePath(VirtualPath);
            if (Resolved.empty()) return 0;
            std::error_code Ec;
            const auto Ftime = std::filesystem::last_write_time(
                std::filesystem::path(Resolved.c_str(), Resolved.c_str() + Resolved.size()), Ec);
            if (Ec) return 0;
            const auto Dur = Ftime.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(Dur).count();
        }

        // Quick xxh64 of the on-disk asset bytes. Reads via VFS so it works
        // for any mounted alias (including plugin /<PluginName>/Content).
        uint64 ContentHashOf(FStringView VirtualPath, TVector<uint8>* OutBytes = nullptr)
        {
            TVector<uint8> Bytes;
            if (!VFS::ReadFile(Bytes, VirtualPath))
            {
                return 0;
            }
            const uint64 H = Hash::XXHash::GetHash64(Bytes.data(), Bytes.size());
            if (OutBytes) *OutBytes = Move(Bytes);
            return H;
        }

        // Recognize /<Alias>/... and check if Alias matches a known plugin
        // name. Returns the plugin name (without slash) or empty.
        FName ExtractOwningPlugin(FStringView VirtualPath)
        {
            if (VirtualPath.empty() || VirtualPath[0] != '/')
            {
                return FName();
            }
            size_t SecondSlash = VirtualPath.find('/', 1);
            if (SecondSlash == FStringView::npos)
            {
                return FName();
            }
            FStringView Alias = VirtualPath.substr(1, SecondSlash - 1);
            // /Game and /Engine never belong to a plugin.
            if (Alias == "Game" || Alias == "Engine" || Alias == "Config")
            {
                return FName();
            }
            // FPluginManager keys plugins by FName(Plugin->GetName()); the
            // alias for plugin content IS the plugin name.
            if (FPluginManager::Get().FindPlugin(Alias) != nullptr)
            {
                return FName(Alias);
            }
            return FName();
        }
    }

    void FAssetRegistry::RunInitialDiscovery()
    {
        LUMINA_PROFILE_SCOPE();

        // Load cached registry first; the discovery pass below only
        // touches entries whose mtime/content changed.
        const bool bHadCache = LoadCache();
        if (!bHadCache)
        {
            ClearAssets();
        }

        TVector<FFixedString> PackagePaths;
        TVector<FFixedString> WalkedRoots;
        PackagePaths.reserve(256);
        WalkedRoots.reserve(8);

        auto Callback = [&](const VFS::FFileInfo& File)
        {
            if (File.IsDirectory()) return;
            if (File.IsLAsset())    PackagePaths.emplace_back(File.VirtualPath);
        };

        WalkedRoots.emplace_back(FFixedString("/Engine/Resources/Content"));
        VFS::RecursiveDirectoryIterator("/Engine/Resources/Content", Callback);

        WalkedRoots.emplace_back(FFixedString("/Game"));
        VFS::RecursiveDirectoryIterator("/Game", Callback);

        // Plugin content: walk every enabled plugin's mount alias. The
        // PluginManager already mounted these into VFS at /<PluginName>.
        for (const FPlugin* Plugin : FPluginManager::Get().GetAllPlugins())
        {
            if (!Plugin->IsEnabled())                  continue;
            if (!Plugin->IsContentMounted())           continue;
            const FString MountAlias = Plugin->GetMountAlias();
            WalkedRoots.emplace_back(FFixedString(MountAlias.c_str()));
            VFS::RecursiveDirectoryIterator(MountAlias, Callback);
        }

        // Snapshot so OnInitialDiscoveryCompleted can reap cache entries
        // whose files no longer exist under a walked root.
        LastDiscoveryWalkedRoots  = WalkedRoots;
        LastDiscoveryVisitedPaths = PackagePaths;
        eastl::sort(LastDiscoveryVisitedPaths.begin(), LastDiscoveryVisitedPaths.end());

        const uint32 NumPackages = (uint32)PackagePaths.size();
        if (NumPackages == 0)
        {
            OnInitialDiscoveryCompleted();
            return;
        }

        Task::AsyncTask(NumPackages, NumPackages, [this, PackagePaths = Move(PackagePaths)] (uint32 Start, uint32 End, uint32)
        {
            for (uint32 i = Start; i < End; ++i)
            {
                ProcessPackagePath(PackagePaths[i]);
            }

            if (End == PackagePaths.size())
            {
                OnInitialDiscoveryCompleted();
            }
        });
    }

    void FAssetRegistry::OnInitialDiscoveryCompleted()
    {
        // Drop cache entries whose underlying files vanished between sessions
        // before we hand the registry out or persist it.
        ReapStaleEntries();

        ImGuiX::Notifications::NotifySuccess("Asset Registry Finished Initial Discovery: Num [{}]", Assets.size());
        LOG_INFO("Asset Registry Finished Initial Discovery: Num [{}]", Assets.size());

        // Persist the cache so next launch only re-parses changed assets; skipped if the path won't resolve.
        SaveCache();

        // Reverse map gets built lazily on first GetReferencersOf().
        {
            FWriteScopeLock Lock(ReverseMapMutex);
            bReverseMapDirty = true;
        }

        MainThread::Enqueue([this]
        {
            BroadcastRegistryUpdate();
        });
    }

    bool FAssetRegistry::NeedsReextract(FStringView Path, int64 MTimeNs, uint64 ContentHash) const
    {
        FReadScopeLock Lock(AssetsMutex);
        for (const TUniquePtr<FAssetData>& Data : Assets)
        {
            if (Data->Path == Path)
            {
                // Mtime is the cheap predicate; content hash is the truth.
                // Pass either-or as 0 to force re-extract.
                if (MTimeNs == 0 || ContentHash == 0) return true;
                return Data->SourceMTimeNs != MTimeNs || Data->ContentHash != ContentHash;
            }
        }
        return true; // not yet in registry
    }

    void FAssetRegistry::AssetCreated(const CObject* Asset)
    {
        FFixedString FilePath = Asset->GetPackage()->GetPackagePath();

        auto AssetData = MakeUnique<FAssetData>();
        AssetData->AssetClass    = Asset->GetClass()->GetName();
        AssetData->AssetGUID     = Asset->GetGUID();
        AssetData->AssetName     = Asset->GetName();
        AssetData->Path          = Move(FilePath);
        AssetData->OwningPlugin  = ExtractOwningPlugin(AssetData->Path);

        FWriteScopeLock Lock(AssetsMutex);
        Assets.emplace(Move(AssetData));

        {
            FWriteScopeLock RLock(ReverseMapMutex);
            bReverseMapDirty = true;
        }

        GetOnAssetRegistryUpdated().Broadcast();
    }

    void FAssetRegistry::AssetDeleted(const FGuid& GUID)
    {
        FWriteScopeLock Lock(AssetsMutex);

        auto It = Assets.find_as(GUID, FGuidHash(), FAssetDataGuidEqual());
        if (It == Assets.end())
        {
            LOG_WARN("AssetRegistry::AssetDeleted: GUID not present in registry; ignoring");
            return;
        }

        Assets.erase(It);

        {
            FWriteScopeLock RLock(ReverseMapMutex);
            bReverseMapDirty = true;
        }

        GetOnAssetRegistryUpdated().Broadcast();
    }

    void FAssetRegistry::AssetRenamed(FStringView OldPath, FStringView NewPath)
    {
        FWriteScopeLock Lock(AssetsMutex);

        auto It = eastl::find_if(Assets.begin(), Assets.end(), [&OldPath](const TUniquePtr<FAssetData>& Asset)
        {
            return Asset->Path == OldPath;
        });

        if (It == Assets.end())
        {
            LOG_WARN("AssetRegistry::AssetRenamed: no entry for {}; rename of {} -> {} not reflected in registry until next discovery", OldPath, OldPath, NewPath);
            return;
        }

        // Drop any stale entry already at NewPath (different GUID), else GetAssetByPath is non-deterministic.
        const FGuid RenamedGuid = (*It)->AssetGUID;
        auto Colliding = eastl::find_if(Assets.begin(), Assets.end(), [&](const TUniquePtr<FAssetData>& Asset)
        {
            return Asset->AssetGUID != RenamedGuid && Asset->Path == NewPath;
        });
        if (Colliding != Assets.end())
        {
            LOG_WARN("AssetRegistry::AssetRenamed: dropping stale entry at {} colliding with rename {} -> {}", NewPath, OldPath, NewPath);
            Assets.erase(Colliding);
            // hash_set::erase can invalidate other iterators; re-find.
            It = eastl::find_if(Assets.begin(), Assets.end(), [&OldPath](const TUniquePtr<FAssetData>& Asset)
            {
                return Asset->Path == OldPath;
            });
            if (It == Assets.end()) return;
        }

        const TUniquePtr<FAssetData>& Data = *It;
        Data->Path.assign_convert(NewPath);
        Data->AssetName    = VFS::FileName(NewPath, true);
        Data->OwningPlugin = ExtractOwningPlugin(NewPath);

        GetOnAssetRegistryUpdated().Broadcast();
    }

    void FAssetRegistry::AssetSaved(CObject* Asset)
    {
        // Re-extract the saved package: its ImportTable + content hash may
        // have changed. Reverse map invalidated.
        FFixedString Path = Asset->GetPackage()->GetPackagePath();
        ProcessPackagePath(Path);

        {
            FWriteScopeLock RLock(ReverseMapMutex);
            bReverseMapDirty = true;
        }

        GetOnAssetRegistryUpdated().Broadcast();
    }

    FAssetData* FAssetRegistry::GetAssetByGUID(const FGuid& GUID) const
    {
        FReadScopeLock Lock(AssetsMutex);

        auto It = eastl::find_if(Assets.begin(), Assets.end(), [&](const auto& Data)
        {
            return Data->AssetGUID == GUID;
        });

        return It == Assets.end() ? nullptr : It->get();
    }

    FAssetData* FAssetRegistry::GetAssetByPath(FStringView Path) const
    {
        FReadScopeLock Lock(AssetsMutex);

        FStringView PathNoExt = VFS::RemoveExtension(Path);
        auto It = eastl::find_if(Assets.begin(), Assets.end(), [&](const TUniquePtr<FAssetData>& Data)
        {
            return VFS::RemoveExtension(Data->Path) == PathNoExt;
        });

        return It == Assets.end() ? nullptr : It->get();
    }

    TVector<FAssetData*> FAssetRegistry::FindByPredicate(const TFunction<bool(const FAssetData&)>& Predicate)
    {
        FReadScopeLock Lock(AssetsMutex);

        TVector<FAssetData*> Datas;
        Datas.reserve(Assets.size() / 2);
        for (const TUniquePtr<FAssetData>& Data : Assets)
        {
            if (Predicate(*Data))
            {
                Datas.emplace_back(Data.get());
            }
        }

        return Datas;
    }

    void FAssetRegistry::ReapStaleEntries()
    {
        if (LastDiscoveryWalkedRoots.empty())
        {
            return;
        }

        FWriteScopeLock Lock(AssetsMutex);

        size_t Reaped = 0;
        for (auto It = Assets.begin(); It != Assets.end(); )
        {
            const FFixedString& Path = (*It)->Path;
            const FStringView PathView(Path.c_str(), Path.size());

            bool bUnderWalkedRoot = false;
            for (const FFixedString& Root : LastDiscoveryWalkedRoots)
            {
                const FStringView RootView(Root.c_str(), Root.size());
                if (PathView.starts_with(RootView))
                {
                    bUnderWalkedRoot = true;
                    break;
                }
            }
            if (!bUnderWalkedRoot)
            {
                ++It;
                continue;
            }

            const bool bVisited = eastl::binary_search(
                LastDiscoveryVisitedPaths.begin(),
                LastDiscoveryVisitedPaths.end(),
                Path);

            if (!bVisited)
            {
                It = Assets.erase(It);
                ++Reaped;
            }
            else
            {
                ++It;
            }
        }

        if (Reaped > 0)
        {
            LOG_INFO("AssetRegistry: reaped {} cached entries whose files no longer exist", Reaped);
            FWriteScopeLock RLock(ReverseMapMutex);
            bReverseMapDirty = true;
        }

        LastDiscoveryWalkedRoots.clear();
        LastDiscoveryVisitedPaths.clear();
    }

    void FAssetRegistry::RebuildReverseMap()
    {
        // Caller holds ReverseMapMutex write lock.
        ReverseDepMap.clear();
        FReadScopeLock AssetsLock(AssetsMutex);
        for (const TUniquePtr<FAssetData>& Data : Assets)
        {
            for (const FAssetDependency& Dep : Data->Dependencies)
            {
                ReverseDepMap[Dep.TargetGUID].push_back(Data->AssetGUID);
            }
        }
        bReverseMapDirty = false;
    }

    TVector<FAssetData*> FAssetRegistry::GetReferencersOf(const FGuid& GUID) const
    {
        // Two-phase: rebuild map under write lock if dirty, then read under
        // shared lock. Const-cast is fine — map cache is mutable state.
        {
            FWriteScopeLock RLock(ReverseMapMutex);
            if (bReverseMapDirty)
            {
                const_cast<FAssetRegistry*>(this)->RebuildReverseMap();
            }
        }

        TVector<FAssetData*> Result;
        {
            FReadScopeLock RLock(ReverseMapMutex);
            auto It = ReverseDepMap.find(GUID);
            if (It == ReverseDepMap.end()) return Result;

            FReadScopeLock ALock(AssetsMutex);
            Result.reserve(It->second.size());
            for (const FGuid& ReferrerGuid : It->second)
            {
                auto AIt = Assets.find_as(ReferrerGuid, FGuidHash(), FAssetDataGuidEqual());
                if (AIt != Assets.end())
                {
                    Result.push_back(AIt->get());
                }
            }
        }
        return Result;
    }

    void FAssetRegistry::ProcessPackagePath(FStringView Path)
    {
        // Pre-check mtime + content hash vs cache; hash is over raw compressed bytes, so source or
        // compression-setting changes invalidate it.
        const int64 MTime = FileMTimeNanos(Path);
        TVector<uint8> RawBytes;
        const uint64 Hash = ContentHashOf(Path, &RawBytes);

        if (Hash != 0 && !NeedsReextract(Path, MTime, Hash))
        {
            return; // cache hit
        }

        if (RawBytes.empty())
        {
            LOG_ERROR("AssetRegistry: failed to read {}", Path);
            RecordFailedAsset(Path);
            return;
        }

        // ReadPackageFile decompresses the deflate-with-FCompressedPackageHeader file into Bytes,
        // which holds the FPackageHeader and import/export tables.
        TVector<uint8> Bytes;
        if (!CPackage::ReadPackageFile(Path, Bytes))
        {
            LOG_ERROR("AssetRegistry: failed to decompress {}", Path);
            RecordFailedAsset(Path);
            return;
        }

        if (Bytes.size() < sizeof(FPackageHeader))
        {
            LOG_ERROR("AssetRegistry: {} is too small to be a valid package", Path);
            RecordFailedAsset(Path);
            return;
        }

        FName PackageFileName = VFS::FileName(Path, true);

        FPackageHeader Header;
        FMemoryReader Reader(Bytes);
        Reader << Header;

        if (Header.Tag != PACKAGE_FILE_TAG)
        {
            LOG_ERROR("AssetRegistry: {} is not a valid Lumina package (tag mismatch)", Path);
            RecordFailedAsset(Path);
            return;
        }

        if (Header.Version > GPackageFileLuminaVersion.FileVersion)
        {
            LOG_ERROR("AssetRegistry: {} was saved with engine version {} (current {}); cannot register files from a newer engine", Path, Header.Version, GPackageFileLuminaVersion.FileVersion);
            RecordFailedAsset(Path);
            return;
        }

        Reader.SetFileVersion(Header.Version);

        if (Header.ExportTableOffset < 0 || static_cast<size_t>(Header.ExportTableOffset) > Bytes.size())
        {
            LOG_ERROR("AssetRegistry: {} has out-of-range export table offset", Path);
            RecordFailedAsset(Path);
            return;
        }

        Reader.Seek(Header.ExportTableOffset);

        TVector<FObjectExport> Exports;
        Reader << Exports;

        FObjectExport* Export = eastl::find_if(Exports.begin(), Exports.end(), [&](const FObjectExport& E)
        {
            return E.ObjectName == PackageFileName;
        });

        if (Export == Exports.end())
        {
            LOG_ERROR("AssetRegistry: {} contains no export matching its file name; refusing to register", Path);
            RecordFailedAsset(Path);
            return;
        }

        // Each ImportTable entry is one outbound edge with the type the saver recorded (Hard for direct
        // CObject* refs, Soft for FSoftObjectPath).
        TVector<FAssetDependency> Dependencies;
        if (Header.ImportTableOffset >= 0
            && static_cast<size_t>(Header.ImportTableOffset) <= Bytes.size())
        {
            Reader.Seek(Header.ImportTableOffset);
            TVector<FObjectImport> Imports;
            Reader << Imports;
            Dependencies.reserve(Imports.size());
            for (const FObjectImport& Import : Imports)
            {
                FAssetDependency Dep;
                Dep.TargetGUID = Import.ObjectGUID;
                Dep.Type       = Import.Type;
                Dependencies.emplace_back(Dep);
            }
        }

        auto AssetData = MakeUnique<FAssetData>();
        AssetData->AssetClass     = Export->ClassName;
        AssetData->AssetGUID      = Export->ObjectGUID;
        AssetData->AssetName      = Export->ObjectName;
        AssetData->Path           .assign_convert(Path);
        AssetData->ContentHash    = Hash;
        AssetData->SourceMTimeNs  = MTime;
        AssetData->Dependencies   = Move(Dependencies);
        AssetData->OwningPlugin   = ExtractOwningPlugin(Path);

        FWriteScopeLock Lock(AssetsMutex);
        // Drop any pre-existing entry with this GUID (external move/rename: path changed, GUID stable);
        // else the dup-GUID collision rejects the new entry and leaves a dangling old path.
        auto ExistingByGuid = Assets.find_as(AssetData->AssetGUID, FGuidHash(), FAssetDataGuidEqual());
        if (ExistingByGuid != Assets.end())
        {
            Assets.erase(ExistingByGuid);
        }
        // Then drop any stale entry at this path with a different GUID (rare:
        // user dropped a .lasset with a fresh GUID over an old one).
        auto ExistingByPath = eastl::find_if(Assets.begin(), Assets.end(), [&](const TUniquePtr<FAssetData>& D)
        {
            return D->Path == AssetData->Path;
        });
        if (ExistingByPath != Assets.end())
        {
            Assets.erase(ExistingByPath);
        }

        Assets.emplace(Move(AssetData));
    }

    void FAssetRegistry::RecordFailedAsset(FStringView Path)
    {
        FWriteScopeLock Lock(FailedAssetsMutex);
        FailedAssets.emplace_back(Path.data(), Path.size());
    }

    void FAssetRegistry::ClearAssets()
    {
        FWriteScopeLock Lock(AssetsMutex);

        Assets.clear();

        {
            FWriteScopeLock RLock(ReverseMapMutex);
            ReverseDepMap.clear();
            bReverseMapDirty = false;
        }

        BroadcastRegistryUpdate();
    }

    void FAssetRegistry::BroadcastRegistryUpdate()
    {
        OnAssetRegistryUpdated.Broadcast();
    }

    void FAssetRegistry::WriteToArchive(FArchive& Ar) const
    {
        uint32 Tag = kAssetRegistryCacheTag;
        Ar << Tag;

        FReadScopeLock Lock(AssetsMutex);
        uint32 Count = (uint32)Assets.size();
        Ar << Count;

        for (const TUniquePtr<FAssetData>& Data : Assets)
        {
            Ar << const_cast<FGuid&>(Data->AssetGUID);
            Ar << const_cast<FFixedString&>(Data->Path);
            Ar << const_cast<FName&>(Data->AssetName);
            Ar << const_cast<FName&>(Data->AssetClass);
            Ar << const_cast<uint64&>(Data->ContentHash);
            Ar << const_cast<int64&>(Data->SourceMTimeNs);
            uint32 Flags = (uint32)Data->Flags;
            Ar << Flags;

            uint32 DepCount = (uint32)Data->Dependencies.size();
            Ar << DepCount;
            for (const FAssetDependency& Dep : Data->Dependencies)
            {
                Ar << const_cast<FGuid&>(Dep.TargetGUID);
                uint8 T = (uint8)Dep.Type;
                Ar << T;
            }

            Ar << const_cast<FName&>(Data->OwnerChunk);
            Ar << const_cast<FName&>(Data->OwningPlugin);
        }
    }

    bool FAssetRegistry::LoadFromArchive(FArchive& Ar)
    {
        uint32 Tag = 0;
        Ar << Tag;
        if (Tag != kAssetRegistryCacheTag)
        {
            return false;
        }

        uint32 Count = 0;
        Ar << Count;

        FWriteScopeLock Lock(AssetsMutex);
        Assets.clear();
        Assets.reserve(Count);

        for (uint32 i = 0; i < Count; ++i)
        {
            auto Data = MakeUnique<FAssetData>();
            Ar << Data->AssetGUID;
            Ar << Data->Path;
            Ar << Data->AssetName;
            Ar << Data->AssetClass;
            Ar << Data->ContentHash;
            Ar << Data->SourceMTimeNs;
            uint32 Flags = 0;
            Ar << Flags;
            Data->Flags = (EAssetFlags)Flags;

            uint32 DepCount = 0;
            Ar << DepCount;
            Data->Dependencies.resize(DepCount);
            for (uint32 d = 0; d < DepCount; ++d)
            {
                Ar << Data->Dependencies[d].TargetGUID;
                uint8 T = 0;
                Ar << T;
                Data->Dependencies[d].Type = (EDependencyType)T;
            }

            Ar << Data->OwnerChunk;
            Ar << Data->OwningPlugin;

            Assets.emplace(Move(Data));
        }

        {
            FWriteScopeLock RLock(ReverseMapMutex);
            bReverseMapDirty = true;
        }

        return true;
    }

    void FAssetRegistry::SaveCache() const
    {
        const FString CachePath = AssetDbPath();
        if (CachePath.empty()) return;

        std::error_code Ec;
        std::filesystem::create_directories(
            std::filesystem::path(CachePath.c_str()).parent_path(), Ec);

        TVector<uint8> Bytes;
        FMemoryWriter Writer(Bytes);
        WriteToArchive(Writer);

        if (!FileHelper::SaveArrayToFile(Bytes, CachePath))
        {
            LOG_WARN("AssetRegistry: failed to write cache to {}", CachePath);
        }
    }

    bool FAssetRegistry::LoadCache()
    {
        const FString CachePath = AssetDbPath();
        if (CachePath.empty()) return false;

        // First-launch / fresh-clone: no cache yet. Quiet exit; full rescan is the correct fallback.
        std::error_code Ec;
        if (!std::filesystem::exists(std::filesystem::path(CachePath.c_str()), Ec))
        {
            return false;
        }

        TVector<uint8> Bytes;
        if (!FileHelper::LoadFileToArray(Bytes, CachePath)) return false;
        if (Bytes.size() < sizeof(uint32)) return false;

        FMemoryReader Reader(Bytes);
        if (!LoadFromArchive(Reader))
        {
            LOG_INFO("AssetRegistry: cache at {} is stale (tag mismatch); rebuilding from scratch", CachePath);
            return false;
        }

        LOG_INFO("AssetRegistry: loaded {} entries from cache {}", Assets.size(), CachePath);
        return true;
    }
}
