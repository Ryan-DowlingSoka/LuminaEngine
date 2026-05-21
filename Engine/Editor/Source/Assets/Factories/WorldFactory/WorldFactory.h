#pragma once
#include "World/World.h"
#include "Assets/Factories/Factory.h"
#include "core/object/ObjectMacros.h"
#include "WorldFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CWorldFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CClass* GetAssetClass() const override { return CWorld::StaticClass(); }
        FString GetAssetName() const override { return "World"; }
        FStringView GetDefaultAssetCreationName() override { return "NewWorld"; }
        
        CObject* CreateNew(const FName& Name, CPackage* Package) override;
    
    };
}
