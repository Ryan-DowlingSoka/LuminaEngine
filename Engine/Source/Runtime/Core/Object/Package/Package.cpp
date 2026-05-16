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
#include "miniz.h"


namespace Lumina
{
    IMPLEMENT_INTRINSIC_CLASS(CPackage, CObject, RUNTIME_API)

    FPackageDestroyedDelegate CPackage::OnPackageDestroyed;

    namespace
    {
        struct FCompressedPackageHeader
        {
            uint64 UncompressedSize;
            uint64 CompressedSize;
        };

        // Deflate at MZ_BEST_SPEED, prefixed with sizes for the loader.
        bool CompressPackageBinary(const TVector<uint8>& In, TVector<uint8>& Out)
        {
            mz_ulong Bound = mz_compressBound((mz_ulong)In.size());
            Out.resize(sizeof(FCompressedPackageHeader) + (size_t)Bound);

            mz_ulong OutLen = Bound;
            int Ret = mz_compress2(Out.data() + sizeof(FCompressedPackageHeader), &OutLen,
                In.data(), (mz_ulong)In.size(), MZ_DEFAULT_LEVEL);

            if (Ret != MZ_OK)
            {
                LOG_ERROR("CompressPackageBinary: compress failed (ret={})", Ret);
                Out.clear();
                return false;
            }

            FCompressedPackageHeader CHeader;
            CHeader.UncompressedSize = In.size();
            CHeader.CompressedSize   = OutLen;
            std::memcpy(Out.data(), &CHeader, sizeof(CHeader));

            Out.resize(sizeof(FCompressedPackageHeader) + (size_t)OutLen);
            return true;
        }

        bool DecompressPackageBinary(const TVector<uint8>& Raw, TVector<uint8>& Out)
        {
            if (Raw.size() < sizeof(FCompressedPackageHeader))
            {
                LOG_ERROR("DecompressPackageBinary: file too small ({} bytes)", Raw.size());
                return false;
            }

            FCompressedPackageHeader CHeader;
            std::memcpy(&CHeader, Raw.data(), sizeof(CHeader));

            if (sizeof(FCompressedPackageHeader) + CHeader.CompressedSize != Raw.size())
            {
                LOG_ERROR("DecompressPackageBinary: size mismatch (header={}, file={})",
                    sizeof(FCompressedPackageHeader) + CHeader.CompressedSize, Raw.size());
                return false;
            }

            Out.resize((size_t)CHeader.UncompressedSize);
            mz_ulong OutLen = (mz_ulong)CHeader.UncompressedSize;
            int Ret = mz_uncompress(Out.data(), &OutLen,
                Raw.data() + sizeof(FCompressedPackageHeader), (mz_ulong)CHeader.CompressedSize);

            if (Ret != MZ_OK || OutLen != CHeader.UncompressedSize)
            {
                LOG_ERROR("DecompressPackageBinary: decompress failed (ret={}, got={}, expected={})",
                    Ret, (uint64)OutLen, CHeader.UncompressedSize);
                Out.clear();
                return false;
            }

            return true;
        }
    }

    bool CPackage::ReadPackageFile(FStringView Path, TVector<uint8>& OutBinary)
    {
        TVector<uint8> RawBinary;
        if (!VFS::ReadFile(RawBinary, Path))
        {
            return false;
        }

        return DecompressPackageBinary(RawBinary, OutBinary);
    }

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
    
    void CPackage::OnDestroy()
    {
        
    }

    bool CPackage::Rename(const FName& NewName, CPackage* NewPackage)
    {
        // In-memory rename only; caller (RenamePackage) handles disk. Exported objects survive so live refs stay valid.
        // FName::ToString() returns by value; bind to locals so the FStringViews aren't dangling.
        const FString NewNameStr = NewName.ToString();
        const FString OldNameStr = GetName().ToString();
        FStringView FileName = VFS::FileName(NewNameStr, true);
        FStringView OldFileName = VFS::FileName(OldNameStr, true);
        bool bFileNameDirty = FileName != OldFileName;

        if (bFileNameDirty)
        {
            // Source of truth is the live object set, not ExportTable: SavePackage clears
            // ExportTable on every save, and freshly-created assets may have stale weak ptrs.
            // Missing the rename here writes a desynced export name on the next save.
            const FName OldFileNameAsFName(OldFileName);
            const FName NewFileNameAsFName(FileName);

            TVector<CObject*> PackageObjects;
            PackageObjects.reserve(8);
            GetObjectsWithPackage(this, PackageObjects);

            for (CObject* Object : PackageObjects)
            {
                if (Object && Object->GetName() == OldFileNameAsFName)
                {
                    Object->Rename(NewFileNameAsFName, nullptr);
                }
            }

            for (FObjectExport& Export : ExportTable)
            {
                if (Export.ObjectName == OldFileNameAsFName)
                {
                    Export.ObjectName = NewFileNameAsFName;
                }
            }
        }

        return Super::Rename(NewName, NewPackage);
    }

    CPackage* CPackage::CreatePackage(FStringView Path)
    {
        FFixedString ObjectName = SanitizeObjectName(Path);
        if (FindObject<CPackage>(ObjectName) != nullptr)
        {
            LOG_ERROR("CreatePackage: package {} already exists", Path);
            return nullptr;
        }

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
        // If loaded, route through the live-reference replacement path.
        if (CPackage* Package = FindPackageByPath(Path))
        {
            return DestroyPackage(Package);
        }
        
        TVector<uint8> PackageBlob;
        if (!ReadPackageFile(Path, PackageBlob))
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

        // Best-effort load; corrupt packages still need to be deletable.
        (void)PackageToDestroy->FullyLoad();

        // Resolve primary-asset GUID from the export table; runtime objects may be missing for corrupt packages.
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

        // Broadcast before teardown so observers can release handles while objects are addressable.
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

        // Always rename-then-resave: in-place export-table patching is unsafe (FName length-prefix shifts offsets).
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
            Package->Rename(SavedName, nullptr);
            return false;
        }

        // Stale duplicate on remove failure is preferable to data loss.
        if (VFS::Exists(OldPath) && !VFS::Remove(OldPath))
        {
            LOG_ERROR("RenamePackage: failed to remove old file {} (new file at {} is intact)", OldPath, NewPath);
        }
        return true;
    }

    void CPackage::OnPackageMovedExternally(FStringView OldPath, FStringView NewPath)
    {
        // Parent-dir rename: file already at NewPath on disk, just update in-memory identity.
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
        if (ReadPackageFile(Path, FileBinary))
        {
            Package->CreateLoader(FileBinary);
        
            FPackageLoader& Reader = *static_cast<FPackageLoader*>(Package->Loader.get());
        
            FPackageHeader PackageHeader;
            Reader << PackageHeader;

            const int64 LoaderSize = Reader.TotalSize();
            auto OffsetInRange = [LoaderSize](int64 Off)
            {
                return Off >= 0 && Off <= LoaderSize;
            };

            if (PackageHeader.Tag != PACKAGE_FILE_TAG)
            {
                LOG_ERROR("LoadPackage: {} is not a valid Lumina package (tag mismatch)", Path);
            }
            else if (PackageHeader.Version > GPackageFileLuminaVersion.FileVersion)
            {
                // Older files load fine — readers branch on Ar.GetFileVersion() to migrate.
                // Newer files we genuinely can't read.
                LOG_ERROR("LoadPackage: {} was saved with engine version {} (current {}); cannot load files from a newer engine", Path, PackageHeader.Version, GPackageFileLuminaVersion.FileVersion);
            }
            else if (!OffsetInRange(PackageHeader.ImportTableOffset) ||
                     !OffsetInRange(PackageHeader.ExportTableOffset) ||
                     !OffsetInRange(PackageHeader.ThumbnailDataOffset))
            {
                LOG_ERROR("LoadPackage: {} has out-of-range header offsets (size={}, import={}, export={}, thumb={})",
                    Path, LoaderSize, PackageHeader.ImportTableOffset, PackageHeader.ExportTableOffset, PackageHeader.ThumbnailDataOffset);
            }
            else
            {
                // Stamp source version so per-type Serialize can branch for migration.
                Reader.SetFileVersion(PackageHeader.Version);

                Reader.Seek(PackageHeader.ImportTableOffset);
                Reader << Package->ImportTable;

                Reader.Seek(PackageHeader.ExportTableOffset);
                Reader << Package->ExportTable;

#if USING(WITH_EDITOR)
                // Non-editor saves encode no thumbnail (offset == 0).
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

        // Skip header; written last once offsets are known.
        Writer.Seek(sizeof(FPackageHeader));

        FSaveContext SaveContext(Package);
        Package->BuildSaveContext(SaveContext);

        // Exports first: serializing them populates Writer's ObjectToIndexMap with the
        // canonical import order. WriteImports then builds ImportTable from that same
        // map, so the indices emitted in export data line up with ImportTable slots.
        Package->WriteExports(Writer, Header, SaveContext);
        Package->WriteImports(Writer, Header, SaveContext);

        Header.ImportCount = static_cast<int32>(Package->ImportTable.size());
        Header.ExportCount = static_cast<int32>(Package->ExportTable.size());

#if USING(WITH_EDITOR)
        Header.ThumbnailDataOffset = Writer.Tell();
        Package->GetPackageThumbnail()->Serialize(Writer);
#else
        // Non-editor: zero signals loader to skip thumbnail.
        Header.ThumbnailDataOffset = 0;
#endif

        Writer.Seek(0);
        Writer << Header;

        TVector<uint8> DiskBinary;
        if (!CompressPackageBinary(FileBinary, DiskBinary))
        {
            LOG_ERROR("Failed to compress package: {}", Path);
            return false;
        }

        if (!VFS::AtomicWriteFile(Path, DiskBinary))
        {
            // Atomic write failed: disk unchanged, package stays dirty for retry.
            LOG_ERROR("Failed to save package: {}", Path);
            return false;
        }

        // Refresh loader only after disk commit so it can't point at uncommitted bytes.
        // Loader always holds the uncompressed bytes.
        Package->CreateLoader(FileBinary);

        LOG_INFO("Saved Package: \"{}\" - ( [{}] Exports | [{}] Imports | [{:.2f}] KiB on disk, [{:.2f}] KiB uncompressed)",
            Package->GetName(),
            Package->ExportTable.size(),
            Package->ImportTable.size(),
            static_cast<double>(DiskBinary.size()) / 1024.0,
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

    void CPackage::WriteImports(FPackageSaver& Ar, FPackageHeader& Header, FSaveContext& SaveContext)
    {
        // Must run AFTER WriteExports — pulls the import order from the saver's ObjectToIndexMap
        // so on-disk indices match what was emitted in export data.
        Ar.PopulateImportTable(ImportTable);

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

        Loader->Seek(SavedPos);
    }

    CObject* CPackage::LoadObject(const FGuid& GUID)
    {
        for (size_t i = 0; i < ExportTable.size(); ++i)
        {
            FObjectExport& Export = ExportTable[i];

            if (Export.ObjectGUID == GUID)
            {
                CObject* Object = FindObjectImpl(Export.ObjectGUID);

                if (Object == nullptr)
                {
                    CClass* ObjectClass = FindObject<CClass>(Export.ClassName);
                    if (ObjectClass == nullptr)
                    {
                        LOG_ERROR("LoadObject: class '{}' for export '{}' in package '{}' could not be resolved", Export.ClassName, Export.ObjectName, GetName());
                        return nullptr;
                    }

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
                    if (ObjectClass == nullptr)
                    {
                        LOG_ERROR("LoadObjectByName: class '{}' for export '{}' in package '{}' could not be resolved", Export.ClassName, Name, GetName());
                        return nullptr;
                    }

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
        bool bAllOk = true;
        for (const FObjectExport& Export : ExportTable)
        {
            if (LoadObject(Export.ObjectGUID) == nullptr)
            {
                bAllOk = false;
            }
        }

        return bAllOk;
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
