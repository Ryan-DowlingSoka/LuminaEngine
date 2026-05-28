#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "EntityComponentType.generated.h"

namespace Lumina
{
    REFLECT()
    class RUNTIME_API CEntityComponentType : public CObject
    {
        GENERATED_BODY()

    public:

        bool IsAsset() const override { return true; }

        void Serialize(FArchive& Ar) override;

        void PostLoad() override;

        FPropertyBag& GetSchemaBag()             { return SchemaBag; }
        const FPropertyBag& GetSchemaBag() const { return SchemaBag; }

        CStruct* GetLayout() const                          { return SchemaBag.GetLayout(); }
        const TVector<FPropertyBagField>& GetFields() const { return SchemaBag.GetSchema(); }
        const void* GetDefaults() const                     { return SchemaBag.GetValueData(); }

        uint32 GetSchemaRevision() const { return SchemaRevision; }
        void   BumpSchemaRevision()      { ++SchemaRevision; }

        uint32 GetStorageId() const;

    private:

        FPropertyBag SchemaBag;
        uint32 SchemaRevision = 1;
    };
}
