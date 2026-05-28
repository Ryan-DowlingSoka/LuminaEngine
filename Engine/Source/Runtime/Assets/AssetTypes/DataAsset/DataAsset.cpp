#include "pch.h"
#include "DataAsset.h"
#include "DataAssetSchema.h"
#include "Core/Object/Class.h"
#include "Core/Object/Package/Package.h"

namespace Lumina
{
    void CDataAsset::PostLoad()
    {
        // Reconcile the just-loaded values against the schema's current layout (it may have
        // changed since this asset was saved).
        SyncToSchema();
    }

    void CDataAsset::Serialize(FArchive& Ar)
    {
        // Reflected properties (the Schema ref) first, then the value bag.
        CObject::Serialize(Ar);
        PropertyBag.Serialize(Ar);
    }

    void CDataAsset::SetSchema(CDataAssetSchema* InSchema)
    {
        Schema = InSchema;
        SyncToSchema();
    }

    void CDataAsset::SyncToSchema()
    {
        if (Schema == nullptr)
        {
            return;
        }

        const FPropertyBag& SchemaBag = Schema->GetSchemaBag();
        PropertyBag.SetSchema(SchemaBag.GetSchema(), &SchemaBag);
    }

    void CDataAsset::OnSchemaDeleted()
    {
        // Only null the (now-dangling) reference. Don't Reset the bag: this can run mid-teardown
        // where allocating a fresh layout is unwise, and the editor already gates its grid on a
        // valid Schema, so the orphaned values stay harmlessly until the asset is re-saved/freed.
        Schema = nullptr;
        if (GetPackage() != nullptr)
        {
            GetPackage()->MarkDirty();
        }
    }
}
