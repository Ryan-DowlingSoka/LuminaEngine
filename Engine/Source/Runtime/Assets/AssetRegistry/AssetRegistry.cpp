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
        VFS::RecursiveDirectoryIterator("/Game/Content", Callback);

        
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
        ASSERT(It != Assets.end());

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

        ASSERT(It != Assets.end());

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

    bool FAssetRegistry::TryRecoverPackage(FStringView Path, TSpan<FObjectExport> Exports)
    {
        bool bRecovered = false;
        for (FObjectExport& Export : Exports)
        {
            CClass* Class = FindObject<CClass>(Export.ClassName);
            if (Class && Class->GetDefaultObject()->IsAsset())
            {
                Export.ObjectName = VFS::FileName(Path, true);
                bRecovered = true;
            }
        }
        
        
        return bRecovered;
    }

    void FAssetRegistry::ProcessPackagePath(FStringView Path)
    {
        TVector<uint8> Data;
        if (!VFS::ReadFile(Data, Path))
        {
            LOG_ERROR("Failed to load package file at path {}", Path);
            return;
        }

        FName PackageFileName = VFS::FileName(Path, true);
        
        FPackageHeader Header;
        FMemoryReader Reader(Data);
        Reader << Header;
        
        if (Header.Version != GPackageFileLuminaVersion.FileVersion)
        {
            LOG_WARN("Package \"{}\" was loaded with a different engine version. {}, it may be unstable.", Path, Header.Version);
        }

        Reader.Seek(Header.ExportTableOffset);
        
        TVector<FObjectExport> Exports;
        Reader << Exports;

        FObjectExport* Export = eastl::find_if(Exports.begin(), Exports.end(), [&](const FObjectExport& E)
        {
            return E.ObjectName == PackageFileName; 
        });
        
        if (ALERT_IF(Export == Exports.end(), "Package name was not found in exports!"))
        {
            if (!TryRecoverPackage(Path, Exports))
            {
                return;
            }
            
            Export = eastl::find_if(Exports.begin(), Exports.end(), [&](const FObjectExport& E)
            {
                return E.ObjectName == PackageFileName; 
            });
            
            if (Export == Exports.end())
            {
                LOG_ERROR("Could not recover package {}", Path);
            }
            
        }
        
        auto AssetData = MakeUnique<FAssetData>();
        AssetData->AssetClass   = Export->ClassName;
        AssetData->AssetGUID    = Export->ObjectGUID;
        AssetData->AssetName    = Export->ObjectName;
        AssetData->Path         .assign_convert(Path);
        

        FWriteScopeLock Lock(AssetsMutex);
        ASSERT(Assets.find(AssetData) == Assets.end());
        Assets.emplace(Move(AssetData));
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
