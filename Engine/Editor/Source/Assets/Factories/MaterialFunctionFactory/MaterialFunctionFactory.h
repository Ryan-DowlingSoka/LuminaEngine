#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/MaterialFunction/MaterialFunction.h"
#include "MaterialFunctionFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CMaterialFunctionFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Material Function"; }
        FStringView GetDefaultAssetCreationName() override { return "NewMaterialFunction"; }
        FString GetAssetDescription() const override { return "A reusable material subgraph with inputs and outputs, usable inside materials."; }
        CClass* GetAssetClass() const override { return CMaterialFunction::StaticClass(); }
        FString GetCategory() const override { return "Material"; }

    };
}
