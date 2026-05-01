#include "pch.h"
#include "Package.h"
#include <utility>
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Object/Archive/ObjectReferenceReplacerArchive.h"
#include "Core/Profiler/Profile.h"
#include "Core/Serialization/Package/PackageLoader.h"
#include "Core/Serialization/Package/PackageSaver.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "TaskSystem/TaskSystem.h"
#include "Thumbnail/PackageThumbnail.h"


namespace Lumina
{
    IMPLEMENT_INTRINSIC_CLASS(CPackage, CObject, RUNTIME_API)

    FPackageDestroyedDelegate CPackage::OnPackageDestroyed;

    FObjectExport::FObjectExport(CObject* InObject)
    {
        ObjectGUID      = InObject->GetGUID();
        ObjectName      = InObject->GetName();
        ClassName       = InObject->GetClass()->GetName();
        Offset          = 0;
        Size            = 0;
        Object          = InObject;
    }

    FObjectImport::FObjectImport(CObject* InObject)
    {
        ObjectGUID      = InObject->GetGUID();
        Object          = InObject;
    }
    
    struct FObjectLoadScopeGuard
    {
        CObject* Object;
        
        FObjectLoadScopeGuard(CObject* InObject)
            : Object(InObject)
        {
            Object->SetFlag(OF_Loading);
        }
        
        ~FObjectLoadScopeGuard()
        {
            Object->ClearFlags(OF_Loading);
        }
        
        bool IsLoading() const { return Object->HasAnyFlag(OF_Loading); }
    };
    
    void CPackage::OnDestroy()
    {
        
    }

    bool CPackage::Rename(const FName& NewName, CPackage* NewPackage)
    {
        // Pure in-memory rename. Disk side (atomic write to new path, removal
        // of the old file) is the responsibility of the caller — typically
        // CPackage::RenamePackage. We deliberately do NOT destroy any exported
        // objects here: live TObjectPtr / TObjectHandle references in the
        // world must keep pointing at the same renamed objects.
        FStringView FileName = VFS::FileName(NewName.ToString(), true);
        FStringView OldFileName = VFS::FileName(GetName().ToString(), true);
        bool bFileNameDirty = FileName != OldFileName;

        if (bFileNameDirty)
        {
            for (FObjectExport& Export : ExportTable)
            {
                if (Export.ObjectName == OldFileName)
                {
                    Export.ObjectName = FileName;
                    if (CObject* Object = Export.Object.Get())
                    {
                        ASSERT(Object->GetName() == OldFileName);
                        Object->Rename(FileName, nullptr);
                        break;
                    }
                }
            }
        }

        return Super::Rename(NewName, NewPackage);
    }

    CPackage* CPackage::CreatePackage(FStringView Path)
    {
        FFixedString ObjectName = SanitizeObjectName(Path);
        ASSERT(FindObject<CPackage>(ObjectName) == nullptr);

        CPackage* Package = NewObject<CPackage>(nullptr, ObjectName);
        Package->AddToRoot();

        LOG_INFO("Created Package: \"{}\"", Path);

        Package->MarkDirty();

        return Package;
    }

    CPackage* CPackage::GetTransientPackage()
    {
        static CPackage* TransientPackage = nullptr;
        if (TransientPackage == nullptr)
        {
            TransientPackage = NewObject<CPackage>(nullptr, "EngineTransient");
            TransientPackage->AddToRoot();
            TransientPackage->SetFlag(OF_Transient);
            TransientPackage->LoadState.store(ELoadState::Loaded, std::memory_order_release);
        }
        return TransientPackage;
    }

    bool CPackage::IsTransientPackage() const
    {
        return HasAnyFlag(OF_Transient);
    }
    
    bool CPackage::DestroyPackage(FStringView Path)
    {
        // If the package is loaded, we need to handle replacing references to its assets.
        if (CPackage* Package = FindPackageByPath(Path))
        {
            return DestroyPackage(Package);
        }
        
        TVector<uint8> PackageBlob;
        if (!VFS::ReadFile(PackageBlob, Path))
        {
            LOG_ERROR("Failed to load package file at path {}", Path);
            return false;
        }
        
        FPackageHeader Header;
        FMemoryReader Reader(PackageBlob);
        Reader << Header;

        Reader.Seek(Header.ExportTableOffset);
        
        TVector<FObjectExport> Exports;
        Reader << Exports;

        FName PackageFileName = VFS::FileName(Path, true);

        TOptional<FObjectExport> Export;
        for (const FObjectExport& E : Exports)
        {
            if (E.ObjectName == PackageFileName)
            {
                Export = E;
                break;
            }
        }

        if (!Export.has_value())
        {
            LOG_ERROR("No primary asset found in package");
            return false;
        }
        
        OnPackageDestroyed.Broadcast(Path);
        FAssetRegistry::Get().AssetDeleted(Export->ObjectGUID);
        VFS::Remove(Path);

        return true;
    }
    
    bool CPackage::DestroyPackage(CPackage* PackageToDestroy)
    {
        if (PackageToDestroy == nullptr || PackageToDestroy->HasAnyFlag(OF_MarkedDestroy))
        {
            return false;
        }

        if (PackageToDestroy->IsTransientPackage())
        {
            LOG_ERROR("DestroyPackage: refusing to destroy the engine transient package");
            return false;
        }

        FFixedString PackagePath = PackageToDestroy->GetPackagePath();

        // Best-effort load. If the file is corrupt or assets failed to deserialize
        // we still need to be able to delete the package.
        (void)PackageToDestroy->FullyLoad();

        // Resolve the primary-asset GUID from the export table (the entry whose
        // name matches the package file name). The export table is the
        // authoritative identity for asset-registry cleanup; runtime objects
        // may be missing or in a partially-constructed state for corrupt
        // packages, so we cannot rely on finding one with IsAsset() == true.
        FName PackageFileName = VFS::FileName(PackagePath, true);
        FGuid AssetGUID;
        for (const FObjectExport& Export : PackageToDestroy->ExportTable)
        {
            if (Export.ObjectName == PackageFileName)
            {
                AssetGUID = Export.ObjectGUID;
                break;
            }
        }

        TVector<CObject*> ExportObjects;
        ExportObjects.reserve(20);
        GetObjectsWithPackage(PackageToDestroy, ExportObjects);

        for (CObject* ExportObject : ExportObjects)
        {
            if (ExportObject == nullptr || ExportObject == PackageToDestroy)
            {
                continue;
            }

            if (ExportObject->HasAnyFlag(OF_MarkedDestroy))
            {
                continue;
            }

            if (ExportObject->IsAsset())
            {
                if (!AssetGUID.IsValid())
                {
                    AssetGUID = ExportObject->GetGUID();
                }

                FObjectReferenceReplacerArchive Ar(ExportObject, nullptr);
                for (TObjectIterator<CObject> Itr; Itr; ++Itr)
                {
                    if (CObject* Object = *Itr)
                    {
                        Object->Serialize(Ar);
                    }
                }
            }
        }

        // Broadcast before tearing the package down so observers can release
        // their handles while the objects are still addressable.
        OnPackageDestroyed.Broadcast(PackagePath);

        if (AssetGUID.IsValid())
        {
            FAssetRegistry::Get().AssetDeleted(AssetGUID);
        }

        for (CObject* ExportObject : ExportObjects)
        {
            if (ExportObject == nullptr || ExportObject == PackageToDestroy)
            {
                continue;
            }

            if (ExportObject->HasAnyFlag(OF_MarkedDestroy))
            {
                continue;
            }

            ExportObject->ConditionalBeginDestroy();
        }

        PackageToDestroy->ExportTable.clear();
        PackageToDestroy->ImportTable.clear();

        PackageToDestroy->RemoveFromRoot();
        PackageToDestroy->ConditionalBeginDestroy();

        if (VFS::Exists(PackagePath) && !VFS::Remove(PackagePath))
        {
            LOG_ERROR("DestroyPackage: failed to remove package file {}", PackagePath);
        }

        return true;
    }

    CPackage* CPackage::FindPackageByPath(FStringView Path)
    {
        FFixedString ObjectName = SanitizeObjectName(Path);
        return FindObject<CPackage>(ObjectName);
    }

    bool CPackage::RenamePackage(FStringView OldPath, FStringView NewPath)
    {
        if (OldPath == NewPath)
        {
            return true;
        }

        if (VFS::Exists(NewPath))
        {
            LOG_ERROR("RenamePackage: destination already exists: {}", NewPath);
            return false;
        }

        if (!VFS::Exists(OldPath))
        {
            LOG_ERROR("RenamePackage: source does not exist: {}", OldPath);
            return false;
        }

        FFixedString OldObjectName = SanitizeObjectName(OldPath);
        FFixedString NewObjectName = SanitizeObjectName(NewPath);

        // Always go through the loaded-rename + full re-save path. Patching an
        // unloaded file's export table in place is unsafe: FName serializes as
        // a length-prefixed string, so the new export table size can differ
        // from the old, which would either clobber the trailing thumbnail
        // block or leave stale bytes — and Header.ThumbnailDataOffset is not
        // recomputed. A clean SavePackage rebuilds every offset from scratch.
        CPackage* Package = FindObject<CPackage>(OldObjectName);
        if (Package == nullptr)
        {
            Package = LoadPackage(OldPath);
            if (Package == nullptr)
            {
                LOG_ERROR("RenamePackage: failed to load {} for rename", OldPath);
                return false;
            }
        }

        FName SavedName = Package->GetName();

        if (!Package->Rename(NewObjectName, nullptr))
        {
            LOG_ERROR("RenamePackage: in-memory rename failed for {}", OldPath);
            return false;
        }

        if (!SavePackage(Package, NewPath))
        {
            LOG_ERROR("RenamePackage: atomic save to {} failed; rolling back in-memory rename", NewPath);
            // Roll back; on-disk state at OldPath is unchanged.
            Package->Rename(SavedName, nullptr);
            return false;
        }

        // New file is committed. Drop the old file. If the remove fails the
        // user just sees a stale duplicate — better than data loss.
        if (VFS::Exists(OldPath) && !VFS::Remove(OldPath))
        {
            LOG_ERROR("RenamePackage: failed to remove old file {} (new file at {} is intact)", OldPath, NewPath);
        }
        return true;
    }

    void CPackage::OnPackageMovedExternally(FStringView OldPath, FStringView NewPath)
    {
        // Used when a parent directory was renamed: the .lasset file is already
        // at NewPath on disk and its content is unchanged (the file name part
        // didn't change, only the directory). We just need to update the
        // in-memory CPackage's identity if it was loaded.
        if (OldPath == NewPath)
        {
            return;
        }

        FFixedString OldObjectName = SanitizeObjectName(OldPath);
        if (CPackage* Package = FindObject<CPackage>(OldObjectName))
        {
            FFixedString NewObjectName = SanitizeObjectName(NewPath);
            Package->Rename(NewObjectName, nullptr);
        }
    }

    CPackage* CPackage::LoadPackage(FStringView Path)
    {
        LUMINA_PROFILE_SCOPE();
        
        FFixedString ObjectName = SanitizeObjectName(Path);
        
        static FMutex FindOrCreateMutex;
        CPackage* Package = nullptr;
        {
            FScopeLock Lock(FindOrCreateMutex);
            Package = FindObject<CPackage>(ObjectName);
            
            if (Package == nullptr)
            {
                Package = NewObject<CPackage>(nullptr, ObjectName);
            }
        }
        
        ELoadState Expected = ELoadState::Unloaded;
        bool bIsLoaderThread = Package->LoadState.compare_exchange_strong(
            Expected, 
            ELoadState::Loading,
            std::memory_order_acquire,
            std::memory_order_acquire);
        
        if (!bIsLoaderThread)
        {
            if (Expected == ELoadState::Loading)
            {
                ELoadState State;
                while ((State = Package->LoadState.load(std::memory_order_acquire)) == ELoadState::Loading)
                {
                    std::atomic_wait(&Package->LoadState, State);
                }
                Expected = State;
            }
        
            return (Expected == ELoadState::Loaded) ? Package : nullptr;
        }
        
        
        bool bSuccess = false;
        auto Start = std::chrono::high_resolution_clock::now();

        TVector<uint8> FileBinary;
        if (VFS::ReadFile(FileBinary, Path))
        {
            Package->CreateLoader(FileBinary);
        
            FPackageLoader& Reader = *static_cast<FPackageLoader*>(Package->Loader.get());
        
            FPackageHeader PackageHeader;
            Reader << PackageHeader;

            if (PackageHeader.Tag == PACKAGE_FILE_TAG)
            {
                Reader.Seek(PackageHeader.ImportTableOffset);
                Reader << Package->ImportTable;
        
                Reader.Seek(PackageHeader.ExportTableOffset);
                Reader << Package->ExportTable;

#if USING(WITH_EDITOR)
                // Thumbnails are editor metadata. Files saved by a non-editor
                // build encode this with ThumbnailDataOffset == 0.
                if (PackageHeader.ThumbnailDataOffset != 0)
                {
                    int64 SizeBefore = Reader.Tell();
                    Reader.Seek(PackageHeader.ThumbnailDataOffset);
                    Package->GetPackageThumbnail()->Serialize(Reader);
                    Reader.Seek(SizeBefore);
                }
#endif

                auto End = std::chrono::high_resolution_clock::now();
                auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(End - Start);
                
                bSuccess = true;
                LOG_INFO("Loaded Package: \"{}\" - ( [{}] Exports | [{}] Imports | [{}] Bytes | [{}] ms)", Package->GetName(), Package->ExportTable.size(), Package->ImportTable.size(), Package->Loader->TotalSize(), Duration);
            }
        }
        
        Package->LoadState.store(bSuccess ? ELoadState::Loaded : ELoadState::Failed, std::memory_order_release);
        
        std::atomic_notify_all(&Package->LoadState);
        
        Package->AddToRoot();
        return Package;
    }

    bool CPackage::SavePackage(CPackage* Package, FStringView Path)
    {
        LUMINA_PROFILE_SCOPE();

        ASSERT(Package != nullptr);

        if (Package->IsTransientPackage())
        {
            LOG_ERROR("SavePackage: refusing to save the engine transient package to {}", Path);
            return false;
        }

        (void)Package->FullyLoad();

        Package->ExportTable.clear();
        Package->ImportTable.clear();

        TVector<uint8> FileBinary;
        FPackageSaver Writer(FileBinary, Package);

        FPackageHeader Header;
        Header.Tag = PACKAGE_FILE_TAG;
        Header.Version = GPackageFileLuminaVersion.FileVersion;

        // Skip the header until we've built the tables.
        Writer.Seek(sizeof(FPackageHeader));

        // Build the save context (imports/exports)
        FSaveContext SaveContext(Package);
        Package->BuildSaveContext(SaveContext);

        Package->WriteImports(Writer, Header, SaveContext);
        Package->WriteExports(Writer, Header, SaveContext);

        Header.ImportCount = static_cast<int32>(Package->ImportTable.size());
        Header.ExportCount = static_cast<int32>(Package->ExportTable.size());

#if USING(WITH_EDITOR)
        Header.ThumbnailDataOffset = Writer.Tell();
        Package->GetPackageThumbnail()->Serialize(Writer);
#else
        // Non-editor builds never persist thumbnail data — flag the absence
        // with a zero offset so the loader knows to skip the read.
        Header.ThumbnailDataOffset = 0;
#endif

        Writer.Seek(0);
        Writer << Header;

        if (!VFS::AtomicWriteFile(Path, FileBinary))
        {
            // Disk file at Path is unchanged thanks to the temp-then-rename
            // primitive. Leave the package marked dirty so the caller (and any
            // future save attempt) knows the on-disk copy is stale.
            LOG_ERROR("Failed to save package: {}", Path);
            return false;
        }

        // Only refresh the loader once the new bytes are guaranteed to be on
        // disk. Otherwise a failed save would leave us with a loader pointing
        // at content that doesn't match what the file system actually holds.
        Package->CreateLoader(FileBinary);

        LOG_INFO("Saved Package: \"{}\" - ( [{}] Exports | [{}] Imports | [{:.2f}] KiB)",
            Package->GetName(),
            Package->ExportTable.size(),
            Package->ImportTable.size(),
            static_cast<double>(FileBinary.size()) / 1024.0);

        Package->ClearDirty();

        return true;
    }

    void CPackage::CreateLoader(const TVector<uint8>& FileBinary)
    {
        void* HeapData = Memory::Malloc(FileBinary.size());
        Memory::Memcpy(HeapData, FileBinary.data(), FileBinary.size());
        Loader = MakeUnique<FPackageLoader>(HeapData, FileBinary.size(), this);
    }

    FPackageLoader* CPackage::GetLoader() const
    {
        return (FPackageLoader*)Loader.get();
    }

    void CPackage::BuildSaveContext(FSaveContext& Context)
    {
        TVector<CObject*> ExportObjects;
        ExportObjects.reserve(20);
        GetObjectsWithPackage(this, ExportObjects);

        FSaveReferenceBuilderArchive Builder(&Context);
        for (CObject* Object : ExportObjects)
        {
            Builder << Object;
        }
    }

    void CPackage::CreateExports()
    {
        while (std::cmp_less(ExportIndex, ExportTable.size()))
        {
            

            ++ExportIndex;
        }
    }

    void CPackage::CreateImports()
    {
        
    }

    void CPackage::WriteImports(FPackageSaver& Ar, FPackageHeader& Header, FSaveContext& SaveContext)
    {
        for (CObject* Import : SaveContext.Imports)
        {
            ImportTable.emplace_back(Import);
        }
        
        Header.ImportTableOffset = Ar.Tell();
        Ar << ImportTable;
        
    }

    void CPackage::WriteExports(FPackageSaver& Ar, FPackageHeader& Header, FSaveContext& SaveContext)
    {
        Header.ObjectDataOffset = Ar.Tell();

        for (CObject* Export : SaveContext.Exports)
        {
            Export->LoaderIndex = FObjectPackageIndex::FromExport((int32)ExportTable.size()).GetRaw();
            ExportTable.emplace_back(Export);
        }

        for (FObjectExport& Export : ExportTable)
        {
            ASSERT(Export.Object.Get() != nullptr);
            
            Export.Offset = Ar.Tell();
            
            Export.Object.Get()->Serialize(Ar);
            
            Export.Size = Ar.Tell() - Export.Offset;
            
        }
        
        Header.ExportTableOffset = Ar.Tell();
        Ar << ExportTable;
    }

    void CPackage::LoadObject(CObject* Object)
    {
        LUMINA_PROFILE_SCOPE();
        if (!Object || !Object->HasAnyFlag(OF_NeedsLoad) || Object->HasAnyFlag(OF_Loading))
        {
            return;
        }
        
        Object->SetFlag(OF_Loading);
        
        CPackage* ObjectPackage = Object->GetPackage();
        
        // If this object's package comes from somewhere else, load it through there.
        if (ObjectPackage != this)
        {
            ObjectPackage->LoadObject(Object);
            return;
        }

        int32 FoundLoaderIndex = FObjectPackageIndex(Object->LoaderIndex).GetArrayIndex();

        if (FoundLoaderIndex < 0 || std::cmp_greater_equal(FoundLoaderIndex, ExportTable.size()))
        {
            LOG_ERROR("Invalid loader index {} for object {}", FoundLoaderIndex, Object->GetName());
            return;
        }

        FObjectExport& Export = ExportTable[FoundLoaderIndex];

        if (!Loader)
        {
            LOG_ERROR("No loader set for package {}", GetName().ToString());
            return;
        }

        const int64 SavedPos = Loader->Tell();
        const int64 DataPos = Export.Offset;
        const int64 ExpectedSize = Export.Size;

        if (DataPos < 0 || ExpectedSize <= 0)
        {
            LOG_ERROR("Invalid export data for object {}. Offset: {}, Size: {}", Object->GetName().ToString(), DataPos, ExpectedSize);
            return;
        }
        
        Loader->Seek(DataPos);
        
        Object->PreLoad();
        
        Object->Serialize(*Loader);
        
        const int64 ActualSize = Loader->Tell() - DataPos;
        
        if (ActualSize != ExpectedSize)
        {
            LOG_WARN("Mismatched size when loading object {}: expected {}, got {}", Object->GetName().ToString(), ExpectedSize, ActualSize);
        }
        
        Object->ClearFlags(OF_NeedsLoad | OF_Loading);
        Object->SetFlag(OF_WasLoaded);

        Object->PostLoad();

        // Reset the state of the loader to the previous object.
        Loader->Seek(SavedPos);
    }

    CObject* CPackage::LoadObject(const FGuid& GUID)
    {
        for (size_t i = 0; i < ExportTable.size(); ++i)
        {
            FObjectExport& Export = ExportTable[i];

            if (Export.ObjectGUID == GUID)
            {
                CClass* ObjectClass = FindObject<CClass>(Export.ClassName);

                CObject* Object = nullptr;
                Object = FindObjectImpl(Export.ObjectGUID);

                if (Object == nullptr)
                {
                    Object = NewObject(ObjectClass, this, Export.ObjectName, Export.ObjectGUID);
                    Object->SetFlag(OF_NeedsLoad);

                    if (Object->IsAsset())
                    {
                        Object->SetFlag(OF_Public);
                    }
                }
            
                Object->LoaderIndex = FObjectPackageIndex::FromExport(static_cast<int32>(i)).GetRaw();

                Export.Object = Object;

                LoadObject(Object);
                
                return Object;
            }
        }

        return nullptr;
    }

    CObject* CPackage::LoadObjectByName(const FName& Name)
    {
        for (size_t i = 0; i < ExportTable.size(); ++i)
        {
            FObjectExport& Export = ExportTable[i];

            if (Export.ObjectName == Name)
            {
                CObject* Object = FindObjectImpl(Export.ObjectGUID);

                if (Object == nullptr)
                {
                    CClass* ObjectClass = FindObject<CClass>(Export.ClassName);
                    Object = NewObject(ObjectClass, this, Export.ObjectName, Export.ObjectGUID);
                    Object->SetFlag(OF_NeedsLoad);
                    
                    if (Object->IsAsset())
                    {
                        Object->SetFlag(OF_Public);
                    }
                }
            
                Object->LoaderIndex = FObjectPackageIndex::FromExport(static_cast<int32>(i)).GetRaw();

                Export.Object = Object;

                LoadObject(Object);
                
                return Object;
            }
        }

        return nullptr;
    }

    bool CPackage::FullyLoad()
    {
        for (const FObjectExport& Export : ExportTable)
        {
            LoadObject(Export.ObjectGUID);
        }

        return true;
    }

    CObject* CPackage::FindObjectInPackage(const FName& Name)
    {
        for (const FObjectExport& Export : ExportTable)
        {
            if (Export.ObjectName == Name)
            {
                return Export.Object.Get();
            }
        }

        return nullptr;
    }

    CObject* CPackage::IndexToObject(const FObjectPackageIndex& Index)
    {
        if (Index.IsNull())
        {
            return nullptr;
        }
        
        if (Index.IsImport())
        {
            size_t ArrayIndex = Index.GetArrayIndex();
            if (ArrayIndex >= ImportTable.size())
            {
                LOG_WARN("Failed to find an object in the import table {}", ArrayIndex);
                return nullptr;
            }

            FObjectImport& Import = ImportTable[ArrayIndex];
            Import.Object = Lumina::LoadObject<CObject>(Import.ObjectGUID);
            
            return ImportTable[ArrayIndex].Object.Get();
        }

        if (Index.IsExport())
        {
            size_t ArrayIndex = Index.GetArrayIndex();
            if (ArrayIndex >= ExportTable.size())
            {
                LOG_WARN("Failed to find an object in the export table {}", ArrayIndex);
                return nullptr;
            }

            return LoadObject(ExportTable[ArrayIndex].ObjectGUID);
        }
        
        return nullptr;
    }

#if USING(WITH_EDITOR)
    FPackageThumbnail* CPackage::GetPackageThumbnail()
    {
        FScopeLock Lock(ThumbnailMutex);

        if (PackageThumbnail == nullptr)
        {
            PackageThumbnail = MakeUnique<FPackageThumbnail>();
        }

        return PackageThumbnail.get();
    }
#endif

    FFixedString CPackage::GetPackagePath() const
    {
        FFixedString Path(GetName().c_str(), GetName().Length());
        AddPackageExt(Path);
        
        return Path;
    }
}
