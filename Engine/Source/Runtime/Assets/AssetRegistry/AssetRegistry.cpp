#include "pch.h"
#include "AssetRegistry.h"

#include "Core/Object/Package/Package.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "TaskSystem/TaskSystem.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    FAssetRegistry& FAssetRegistry::Get()
    {
        static FAssetRegistry Registry;
        return Registry;
    }
    
    void FAssetRegistry::RunInitialDiscovery()
    {
        LUMINA_PROFILE_SCOPE();
        
        ClearAssets();
        
        TVector<FFixedString> PackagePaths;
        PackagePaths.reserve(100);
        
        auto Callback = [&](const VFS::FFileInfo& File)
        {
            if (File.IsDirectory())
            {
                return;
            }
            
            if (File.IsLAsset())
            {
                PackagePaths.emplace_back(File.VirtualPath);
            }
        };
        
        VFS::RecursiveDirectoryIterator("/Engine/Resources/Content", Callback);
        VFS::RecursiveDirectoryIterator("/Game", Callback);

        
        uint32 NumPackages = (uint32)PackagePaths.size();
        Task::AsyncTask(NumPackages, NumPackages, [this, PackagePaths = Move(PackagePaths)] (uint32 Start, uint32 End, uint32)
        {
            for (uint32 i = Start; i < End; ++i)
            {
                const FFixedString& PathString = PackagePaths[i];
                ProcessPackagePath(PathString);
            }
        
            if (End == PackagePaths.size())
            {
                OnInitialDiscoveryCompleted();
            }
        });
    }

    void FAssetRegistry::OnInitialDiscoveryCompleted()
    {
        ImGuiX::Notifications::NotifySuccess("Asset Registry Finished Initial Discovery: Num [{}]", Assets.size());
        LOG_INFO("Asset Registry Finished Initial Discovery: Num [{}]", Assets.size());
    }

    void FAssetRegistry::AssetCreated(const CObject* Asset)
    {
        FFixedString FilePath = Asset->GetPackage()->GetPackagePath();
        
        auto AssetData = MakeUnique<FAssetData>();
        AssetData->AssetClass   = Asset->GetClass()->GetName();
        AssetData->AssetGUID    = Asset->GetGUID();
        AssetData->AssetName    = Asset->GetName();
        AssetData->Path         = Move(FilePath);

        FWriteScopeLock Lock(AssetsMutex);
        Assets.emplace(Move(AssetData));

        GetOnAssetRegistryUpdated().Broadcast();
    }

    void FAssetRegistry::AssetDeleted(const FGuid& GUID)
    {
        FWriteScopeLock Lock(AssetsMutex);

        auto It = Assets.find_as(GUID, FGuidHash(), FAssetDataGuidEqual());
        if (It == Assets.end())
        {
            // Out-of-band deletion (e.g. external file removal followed by a
            // discovery pass that already pruned the entry). Don't crash —
            // the desired end state (entry absent) already holds.
            LOG_WARN("AssetRegistry::AssetDeleted: GUID not present in registry; ignoring");
            return;
        }

        Assets.erase(It);

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
            // The on-disk side of the rename succeeded but our registry view
            // is out of sync (race with discovery, or registry was rebuilt).
            // Crashing the editor is strictly worse than logging — the next
            // discovery pass will repair the entry.
            LOG_WARN("AssetRegistry::AssetRenamed: no entry for {}; rename of {} -> {} not reflected in registry until next discovery", OldPath, OldPath, NewPath);
            return;
        }

        const TUniquePtr<FAssetData>& Data = *It;
        Data->Path.assign_convert(NewPath);
        Data->AssetName = VFS::FileName(NewPath, true);

        GetOnAssetRegistryUpdated().Broadcast();
    }

    void FAssetRegistry::AssetSaved(CObject* Asset)
    {
        FReadScopeLock Lock(AssetsMutex);
        
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

    void FAssetRegistry::ProcessPackagePath(FStringView Path)
    {
        TVector<uint8> Data;
        if (!VFS::ReadFile(Data, Path))
        {
            LOG_ERROR("AssetRegistry: failed to read {}", Path);
            RecordFailedAsset(Path);
            return;
        }

        if (Data.size() < sizeof(FPackageHeader))
        {
            LOG_ERROR("AssetRegistry: {} is too small to be a valid package", Path);
            RecordFailedAsset(Path);
            return;
        }

        FName PackageFileName = VFS::FileName(Path, true);

        FPackageHeader Header;
        FMemoryReader Reader(Data);
        Reader << Header;

        if (Header.Tag != PACKAGE_FILE_TAG)
        {
            LOG_ERROR("AssetRegistry: {} is not a valid Lumina package (tag mismatch)", Path);
            RecordFailedAsset(Path);
            return;
        }

        if (Header.Version != GPackageFileLuminaVersion.FileVersion)
        {
            LOG_ERROR("AssetRegistry: {} was saved with engine version {} (current {}); refusing to register until migrated", Path, Header.Version, GPackageFileLuminaVersion.FileVersion);
            RecordFailedAsset(Path);
            return;
        }

        if (Header.ExportTableOffset < 0 || static_cast<size_t>(Header.ExportTableOffset) > Data.size())
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
            // The .lasset's primary export name doesn't match the file name on
            // disk. We used to silently patch this; that hid the underlying
            // bug (non-atomic save / rename) that left these on disk in the
            // first place. With atomic save+rename in place this should never
            // happen for new files, so we surface it loudly instead.
            LOG_ERROR("AssetRegistry: {} contains no export matching its file name; refusing to register", Path);
            RecordFailedAsset(Path);
            return;
        }

        auto AssetData = MakeUnique<FAssetData>();
        AssetData->AssetClass   = Export->ClassName;
        AssetData->AssetGUID    = Export->ObjectGUID;
        AssetData->AssetName    = Export->ObjectName;
        AssetData->Path         .assign_convert(Path);


        FWriteScopeLock Lock(AssetsMutex);
        if (Assets.find(AssetData) != Assets.end())
        {
            // Duplicate GUID across two .lasset files. Discovery is racy with
            // user-driven renames so don't assert; flag the offender so the
            // editor can surface it.
            LOG_ERROR("AssetRegistry: duplicate asset GUID encountered while processing {} (already registered); skipping", Path);
            RecordFailedAsset(Path);
            return;
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

        BroadcastRegistryUpdate();
    }

    void FAssetRegistry::BroadcastRegistryUpdate()
    {
        OnAssetRegistryUpdated.Broadcast();
    }
}
