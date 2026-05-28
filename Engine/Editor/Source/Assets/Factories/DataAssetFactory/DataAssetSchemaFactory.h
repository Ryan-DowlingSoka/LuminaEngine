#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/DataAsset/DataAssetSchema.h"
#include "DataAssetSchemaFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CDataAssetSchemaFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Data Asset Schema"; }
        FStringView GetDefaultAssetCreationName() override { return "NewDataAssetSchema"; }
        FString GetAssetDescription() const override { return "Defines the fields (and defaults) shared by a family of data assets."; }
        CClass* GetAssetClass() const override { return CDataAssetSchema::StaticClass(); }
        FString GetCategory() const override { return "Data"; }
    };
}
