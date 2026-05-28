#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "DataAssetSchema.h"
#include "DataAsset.generated.h"

namespace Lumina
{
    /**
     * A designer-authored data instance: a reference to a CDataAssetSchema plus the values
     * for that schema's fields, held in an FPropertyBag. The editor for it is just a property
     * grid over those values -- the structure is owned and edited by the schema. SyncToSchema
     * rebuilds the value layout to match the schema (preserving values by name), so schema
     * edits flow into instances and a stale on-disk layout is reconciled on load.
     */
    REFLECT()
    class RUNTIME_API CDataAsset : public CObject
    {
        GENERATED_BODY()

    public:

        bool IsAsset() const override { return true; }

        void PostLoad() override;
        void Serialize(FArchive& Ar) override;

        CDataAssetSchema* GetSchema() const { return Schema; }

        // Assigns the schema and immediately rebuilds the value layout to match it.
        void SetSchema(CDataAssetSchema* InSchema);

        // Rebuilds the value bag to the schema's current field list, keeping existing values
        // by name and seeding new fields from the schema's defaults. No-op without a schema.
        void SyncToSchema();

        // Called when the referenced schema is deleted: drops the (now dangling) reference and
        // clears the value bag so the asset stops presenting an ownerless layout.
        void OnSchemaDeleted();

        FPropertyBag& GetPropertyBag()             { return PropertyBag; }
        const FPropertyBag& GetPropertyBag() const { return PropertyBag; }

        // Serialized; set at creation. Not marked Editable, so it stays out of the value grid.
        PROPERTY()
        TObjectPtr<CDataAssetSchema> Schema;

    private:

        // The instance's values; layout mirrors Schema. Not a reflected PROPERTY.
        FPropertyBag PropertyBag;
    };
}
