#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/PhysicsMaterial/PhysicsMaterial.h"
#include "PhysicsMaterialFactory.generated.h"


namespace Lumina
{
    REFLECT()
    class CPhysicsMaterialFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Physics Material"; }
        FStringView GetDefaultAssetCreationName() override { return "NewPhysicsMaterial"; }
        FString GetAssetDescription() const override { return "Surface friction, restitution, and combine rules assigned to colliders."; }
        CClass* GetAssetClass() const override { return CPhysicsMaterial::StaticClass(); }
        FString GetCategory() const override { return "Physics"; }
    };
}
