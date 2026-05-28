#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/DataAsset/DataAsset.h"
#include "GUID/GUID.h"
#include "DataAssetFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CDataAssetFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Data Asset"; }
        FStringView GetDefaultAssetCreationName() override { return "NewDataAsset"; }
        FString GetAssetDescription() const override { return "An instance of a Data Asset Schema: values for that schema's fields."; }
        CClass* GetAssetClass() const override { return CDataAsset::StaticClass(); }
        FString GetCategory() const override { return "Data"; }

        // Pick the schema the new data asset will instance.
        bool HasCreationDialogue() const override;
        bool DrawCreationDialogue(FStringView Path, bool& bShouldClose) override;

    private:

        FGuid SelectedSchemaGUID;
    };
}
