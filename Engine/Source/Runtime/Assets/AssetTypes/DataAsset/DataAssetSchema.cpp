#include "pch.h"
#include "DataAssetSchema.h"
#include "DataAsset.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectArray.h"

namespace Lumina
{
    void CDataAssetSchema::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
        SchemaBag.Serialize(Ar);
    }

    void CDataAssetSchema::PropagateToInstances()
    {
        GObjectArray.ForEachObject([this](CObjectBase* Object, int32)
        {
            if (Object == nullptr || !Object->IsA<CDataAsset>())
            {
                return;
            }

            CDataAsset* DataAsset = static_cast<CDataAsset*>(Object);
            if (DataAsset->GetSchema() == this)
            {
                DataAsset->SyncToSchema();
            }
        });
    }

    void CDataAssetSchema::OnDestroy()
    {
        // Runs before this object is freed, so we can safely null the back-references.
        GObjectArray.ForEachObject([this](CObjectBase* Object, int32)
        {
            if (Object == nullptr || !Object->IsA<CDataAsset>())
            {
                return;
            }

            CDataAsset* DataAsset = static_cast<CDataAsset*>(Object);
            if (DataAsset->GetSchema() == this)
            {
                DataAsset->OnSchemaDeleted();
            }
        });
    }
}
