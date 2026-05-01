#pragma once

#include "Lumina.h"
#include "Core/Delegates/Delegate.h"
#include "Core/Object/Class.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "Core/Serialization/Package/PackageSaver.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    struct FPackageThumbnail;
    class FSaveContext;
    class FPackageLoader;
}

#define PACKAGE_FILE_TAG 0x9E2A83C1

namespace Lumina
{
    DECLARE_MULTICAST_DELEGATE(FPackageDestroyedDelegate, FName);
    
    struct FObjectExport
    {
        FObjectExport() = default;
        FObjectExport(CObject* InObject);

        /** Globally unique ID of the object */
        FGuid ObjectGUID;

        /** Name of the object */
        FName ObjectName;
        
        /** The class of the object (e.g., "CStaticMesh") */
        FName ClassName;

        /** Offset into the package data where the serialized object begins (for project packages) */
        int64 Offset;

        /** Size in bytes of the serialized object */
        int64 Size;
        
        /** The object which may have been loaded into the package */
        TWeakObjectPtr<CObject> Object;

        FORCEINLINE friend FArchive& operator << (FArchive& Ar, FObjectExport& Data)
        {
            Ar << Data.ObjectGUID;
            Ar << Data.ObjectName;
            Ar << Data.ClassName;
            Ar << Data.Offset;
            Ar << Data.Size;
            
            return Ar;
        }
    };

    struct FObjectImport
    {
        FObjectImport() = default;
        FObjectImport(CObject* InObject);
       

        /** Globally unique ID of the object */
        FGuid ObjectGUID;

        /** Runtime-resolved pointer after import is loaded */
        TWeakObjectPtr<CObject> Object;

        FORCEINLINE friend FArchive& operator << (FArchive& Ar, FObjectImport& Data)
        {
            Ar << Data.ObjectGUID;
            
            return Ar;
        }
    };
    

    struct FPackageHeader
    {
        /** Tag matching PACKAGE_FILE_TAG to make sure this file is a Lumina package */
        uint32 Tag;
        
        /** File format version. */
        int32 Version;

        /** Byte offset from file start to the import table section */
        int64 ImportTableOffset;

        /** Number of entries in the import table */
        int32 ImportCount;

        /** Byte offset from file start to the export table section */
        int64 ExportTableOffset;

        /** Number of entries in the export table */
        int32 ExportCount;

        /** Byte offset from file start to the raw object data block */
        int64 ObjectDataOffset;

        /** Byte offset from the file start to the thumbnail */
        int64 ThumbnailDataOffset;

        friend FArchive& operator << (FArchive& Ar, FPackageHeader& Data)
        {
            Ar << Data.Tag;
            Ar << Data.Version;
            Ar << Data.ImportTableOffset;
            Ar << Data.ImportCount;
            Ar << Data.ExportTableOffset;
            Ar << Data.ExportCount;
            Ar << Data.ObjectDataOffset;
            Ar << Data.ThumbnailDataOffset;

            return Ar;
        }
    };
    static_assert(std::is_standard_layout_v<FPackageHeader>, "FPackageHeader must only contain trivial data members");
    static_assert(std::is_trivially_copyable_v<FPackageHeader>, "FPackageHeader must only contain trivial data members");
    
    /**
     * Stores either a negative number to represent an import index,
     * or a positive number to represent an export index.
     * 0 represents null (no reference).
     * Use IsImport(), IsExport(), IsNull(), and GetArrayIndex() for safe access.
     */
    struct FObjectPackageIndex
    {
    public:
        
        FObjectPackageIndex() : Index(0) {}

        // Construct from raw index (Import: -(i+1), Export: i+1)
        explicit FObjectPackageIndex(int32 InIndex) : Index(InIndex) {}

        // Construct an import index from array index
        static FObjectPackageIndex FromImport(int32 ImportArrayIndex)
        {
            return FObjectPackageIndex(-(ImportArrayIndex + 1));
        }

        // Construct an export index from array index
        static FObjectPackageIndex FromExport(int32 ExportArrayIndex)
        {
            return FObjectPackageIndex(ExportArrayIndex + 1);
        }

        // Check if the index is null (no reference)
        bool IsNull() const
        {
            return Index == 0;
        }

        // Check if it's an import
        bool IsImport() const
        {
            return Index < 0;
        }

        // Check if it's an export
        bool IsExport() const
        {
            return Index > 0;
        }

        // Get the raw internal value
        int32 GetRaw() const
        {
            return Index;
        }

        // Returns the usable array index for ImportTable or ExportTable
        int32 GetArrayIndex() const
        {
            if (IsNull())
            {
                return INDEX_NONE;
            }

            return IsExport() ? (Index - 1) : (-Index - 1);
        }

        bool operator==(const FObjectPackageIndex& Other) const { return Index == Other.Index; }
        bool operator!=(const FObjectPackageIndex& Other) const { return Index != Other.Index; }

        FORCEINLINE friend FArchive& operator << (FArchive& Ar, FObjectPackageIndex& Data)
        {
            Ar << Data.Index;
            
            return Ar;
        }
        
    private:
        
        int32 Index;
    };

    //---------------------------------------------------------------------------------
    
    class CPackage : public CObject
    {
    public:

        DECLARE_CLASS(Lumina, CPackage, CObject, "" /** Intentionally empty */, RUNTIME_API)
        DEFINE_CLASS_FACTORY(CPackage)
        
        enum class ELoadState
        {
            Unloaded,
            Loading,
            Loaded,
            Failed
        };

        void OnDestroy() override;
        bool Rename(const FName& NewName, CPackage* NewPackage) override;
        
        /**
         * 
         * @param FileName 
         * @return 
         */
        RUNTIME_API static CPackage* CreatePackage(FStringView Path);

        /**
         * Returns the engine-wide in-memory package that holds runtime-only
         * objects which still need stable identity across save/load cycles.
         * Engine primitive meshes (Cube/Sphere/etc.), default materials, and
         * other procedurally-built assets live here so worlds can reference
         * them by GUID without the asset registry knowing about a file.
         *
         * Lazily created on first call, rooted, never written to disk, and
         * may not be passed to SavePackage / DestroyPackage.
         */
        RUNTIME_API static CPackage* GetTransientPackage();

        /** True if this package is the engine-wide in-memory package. */
        RUNTIME_API bool IsTransientPackage() const;

        RUNTIME_API static bool DestroyPackage(FStringView Path);

        RUNTIME_API static bool DestroyPackage(CPackage* PackageToDestroy);

        RUNTIME_API static CPackage* FindPackageByPath(FStringView Path);

        /**
         * Canonical rename entry point. Owns both the in-memory rename and the
         * on-disk move. Performs an atomic write of the new file then removes
         * the old one, so a crash mid-rename leaves either the old file or the
         * new one fully intact — never both half-written.
         *
         * Returns false on any failure (collision, I/O error, missing source,
         * tag mismatch). Caller must check the return value before updating
         * the asset registry.
         */
        RUNTIME_API NODISCARD static bool RenamePackage(FStringView OldPath, FStringView NewPath);

        /**
         * Notify the package layer that the .lasset file at OldPath is now at
         * NewPath because a parent directory was renamed externally (e.g.
         * folder rename in the content browser). Updates the in-memory
         * CPackage's identity if loaded; does not touch disk.
         *
         * Assumes the file name part is unchanged — only the directory
         * portion of the path differs.
         */
        RUNTIME_API static void OnPackageMovedExternally(FStringView OldPath, FStringView NewPath);


        /**
         * Will load the package and create the package loader. If called twice, nothing will happen.
         * Objects loaded from this package are not yet serialized, and are essentially shells marked with OF_NeedsLoad.
         * @param FileName File name to load the linker from.
         * @return Loaded package.
         */
        RUNTIME_API static CPackage* LoadPackage(FStringView Path);
        
        /**
         * Saves one specific object to disk.
         * @param Package Package to save.
         * @param FileName Full filename.
         *
         * @return true if package saved successfully.
         */
        RUNTIME_API static bool SavePackage(CPackage* Package, FStringView Path);

        void CreateLoader(const TVector<uint8>& FileBinary);
        
        RUNTIME_API FPackageLoader* GetLoader() const;

        RUNTIME_API void BuildSaveContext(FSaveContext& Context);

        RUNTIME_API void CreateExports();
        RUNTIME_API void CreateImports();

        void WriteImports(FPackageSaver& Ar, FPackageHeader& Header, FSaveContext& SaveContext);
        void WriteExports(FPackageSaver& Ar, FPackageHeader& Header, FSaveContext& SaveContext);
                
        /**
         * Actually serialize the object from this package. After this function is finished,
         * the object will be fully loaded. If it's called without OF_NeedsLoad, data serialization
         * will be skipped.
         * 
         * @param Object Object to load
         * @return If load was successful.
         */
        RUNTIME_API void LoadObject(CObject* Object);
        RUNTIME_API CObject* LoadObject(const FGuid& GUID);
        RUNTIME_API CObject* LoadObjectByName(const FName& Name);

        /**
         * Load all the objects in this package (serialize).
         * @return If all object loads were successful.
         */
        RUNTIME_API NODISCARD bool FullyLoad();

        RUNTIME_API CObject* FindObjectInPackage(const FName& Name);
        
        RUNTIME_API NODISCARD CObject* IndexToObject(const FObjectPackageIndex& Index);

#if USING(WITH_EDITOR)
        /** Returns the thumbnail data for this package. Editor-only — thumbnails
         *  are editor metadata and are not present in non-editor builds. */
        RUNTIME_API NODISCARD FPackageThumbnail* GetPackageThumbnail();
#endif

        RUNTIME_API NODISCARD FFixedString GetPackagePath() const;
        
        RUNTIME_API void MarkDirty() { bDirty = true; }
        RUNTIME_API void ClearDirty() { bDirty = false; }
        RUNTIME_API NODISCARD bool IsDirty() const { return bDirty; }
        
        template<typename T>
        static void AddPackageExt(T& String)
        {
            String += ".lasset";
        }
        
    public:
        
        RUNTIME_API static FPackageDestroyedDelegate OnPackageDestroyed;

        uint32                           bDirty:1 = false;
        
        TUniquePtr<FArchive>             Loader;
        TVector<FObjectImport>           ImportTable;
        TVector<FObjectExport>           ExportTable;
        
        int64       ExportIndex = 0;
        
    private:

        TAtomic<ELoadState>             LoadState{ELoadState::Unloaded};
#if USING(WITH_EDITOR)
        mutable FMutex                  ThumbnailMutex;
        TUniquePtr<FPackageThumbnail>   PackageThumbnail;
#endif
    };
    
}
