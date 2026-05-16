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

    void FPackageSaver::PopulateImportTable(TVector<FObjectImport>& Out) const
    {
        Out.clear();
        Out.resize(CurrentImportIndex);
        for (const auto& [Obj, Idx] : ObjectToIndexMap)
        {
            Out[Idx] = FObjectImport(Obj);
        }
    }
}
