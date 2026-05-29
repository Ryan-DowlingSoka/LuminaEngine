#include "pch.h"
#include "PackageSaver.h"

#include "Core/Object/Package/Package.h"

namespace Lumina
{
    bool FSaveContext::AddExport(CObject* Export)
    {
        if (!SeenExports.insert(Export).second)
        {
            return false;
        }

        Exports.push_back(Export);
        return true;
    }

    FArchive& FSaveReferenceBuilderArchive::operator<<(CObject*& Value)
    {
        if (Value == nullptr || Value->GetPackage() == nullptr)
        {
            return *this;
        }

        // Only recurse into same-package exports, and only on first sight.
        // Skipping the guard re-enters Serialize for shared/cyclic refs and infinite-loops.
        if (Value->GetPackage() == SaveContext->CurrentPackage)
        {
            if (SaveContext->AddExport(Value))
            {
                Value->Serialize(*this);
            }
        }

        return *this;
    }

    FArchive& FSaveReferenceBuilderArchive::operator<<(FObjectHandle& Value)
    {
        if (CObject* Object = Value.Resolve())
        {
            return FSaveReferenceBuilderArchive::operator<<(Object);
        }

        return *this;
    }

    //---------------------------------------------------------------------------------------------
    
    FArchive& FPackageSaver::operator<<(CObject*& Value)
    {
        FObjectPackageIndex Index;
        if (Value)
        {
            if (Value->GetPackage() == Package)
            {
                Index = FObjectPackageIndex(Value->GetLoaderIndex());
            }
            else
            {
                if (ObjectToIndexMap.find(Value) != ObjectToIndexMap.end())
                {
                    Index = FObjectPackageIndex::FromImport(ObjectToIndexMap[Value]);
                }
                else
                {
                    Index = FObjectPackageIndex::FromImport(CurrentImportIndex);
                    ObjectToIndexMap.emplace(Value, CurrentImportIndex);
                    CurrentImportIndex++;
                }
            }
        }
        
        *this << Index;
        
        return *this;
    }

    FArchive& FPackageSaver::operator<<(FObjectHandle& Value)
    {
        FObjectPackageIndex Index;
        if (CObject* Obj = Value.Resolve())
        {
            if (Obj->GetPackage() == Package)
            {
                Index = FObjectPackageIndex(Obj->GetLoaderIndex());
            }
            else
            {
                if (ObjectToIndexMap.find(Obj) != ObjectToIndexMap.end())
                {
                    Index = FObjectPackageIndex::FromImport(ObjectToIndexMap[Obj]);
                }
                else
                {
                    Index = FObjectPackageIndex::FromImport(CurrentImportIndex);
                    ObjectToIndexMap.emplace(Obj, CurrentImportIndex);
                    CurrentImportIndex++;
                }
            }
        }

        *this << Index;

        return *this;
    }

    void FPackageSaver::RegisterSoftAssetReference(const FGuid& AssetGUID)
    {
        if (!AssetGUID.IsValid())
        {
            return;
        }
        SoftReferencedGUIDs.insert(AssetGUID);
    }

    void FPackageSaver::PopulateImportTable(TVector<FObjectImport>& Out) const
    {
        Out.clear();

        // Hard imports first: same slots they were assigned at write time
        // (so any FObjectPackageIndex referencing them in the data stream
        // still points at the right entry).
        Out.resize(CurrentImportIndex);
        THashSet<FGuid> HardGUIDs;
        HardGUIDs.reserve(ObjectToIndexMap.size());
        for (const auto& [Obj, Idx] : ObjectToIndexMap)
        {
            FObjectImport Entry(Obj);
            Entry.Type = EDependencyType::Hard;
            HardGUIDs.insert(Entry.ObjectGUID);
            Out[Idx] = Move(Entry);
        }

        // Append soft imports that weren't already pulled in as hard. They
        // have no data-stream index — the FSoftObjectPath already wrote its
        // own Path+GUID inline. They live here purely so AssetRegistry +
        // FCookGraph see a typed dependency edge.
        //
        // Sort first so cook output is reproducible: SoftReferencedGUIDs is
        // a hash_set and iterates in undefined order; without sorting,
        // identical sources can produce byte-different packages.
        TVector<FGuid> SortedSoft;
        SortedSoft.reserve(SoftReferencedGUIDs.size());
        for (const FGuid& Guid : SoftReferencedGUIDs)
        {
            if (HardGUIDs.find(Guid) != HardGUIDs.end()) continue;
            SortedSoft.push_back(Guid);
        }
        eastl::sort(SortedSoft.begin(), SortedSoft.end());
        for (const FGuid& Guid : SortedSoft)
        {
            Out.emplace_back(Guid, EDependencyType::Soft);
        }
    }
}
