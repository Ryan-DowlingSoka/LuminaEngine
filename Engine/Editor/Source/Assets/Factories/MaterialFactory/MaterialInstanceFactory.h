#pragma once
#include "Assets/AssetTypes/Material/MaterialInstance.h"
#include "Assets/Factories/Factory.h"
#include "GUID/GUID.h"
#include "MaterialInstanceFactory.generated.h"


namespace Lumina
{
    REFLECT()
    class CMaterialInstanceFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;
        CClass* GetAssetClass() const override { return CMaterialInstance::StaticClass(); }
        FString GetCategory() const override { return "Material"; }
        FString GetAssetName() const override { return "Material Instance"; }
        FStringView GetDefaultAssetCreationName() override { return "NewMaterialInstance"; }

        FString GetAssetDescription() const override { return "An instance of a material."; }

        bool HasCreationDialogue() const override;
        bool DrawCreationDialogue(FStringView Path, bool& bShouldClose) override;

    private:

        FGuid SelectedMaterialGUID;
    };
}
