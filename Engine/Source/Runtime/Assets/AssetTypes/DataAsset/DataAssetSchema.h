#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "DataAssetSchema.generated.h"

namespace Lumina
{
    /**
     * Defines the shape (and default values) of a family of data assets.
     *
     * The schema owns an FPropertyBag whose layout is the field list and whose value buffer
     * holds the per-field defaults. A CDataAsset references a schema and mirrors this layout
     * into its own bag, seeding new fields from these defaults. Editing the schema re-syncs
     * every loaded instance (PropagateToInstances), so the structure is authored in exactly
     * one place -- like Unreal's UBlackboardData vs the per-owner blackboard.
     */
    REFLECT()
    class RUNTIME_API CDataAssetSchema : public CObject
    {
        GENERATED_BODY()

    public:

        bool IsAsset() const override { return true; }

        void Serialize(FArchive& Ar) override;

        // Deleting the schema orphans its instances; clear their reference + values here so
        // nothing dangles or keeps showing a layout whose owner is gone.
        void OnDestroy() override;

        FPropertyBag& GetSchemaBag()             { return SchemaBag; }
        const FPropertyBag& GetSchemaBag() const { return SchemaBag; }

        // Re-syncs every loaded CDataAsset that references this schema to the current layout.
        // Editor-only in practice (schemas aren't edited at runtime); safe to call anytime.
        void PropagateToInstances();

    private:

        // Field list + per-field defaults. Not a reflected PROPERTY; serialized manually.
        FPropertyBag SchemaBag;
    };
}
