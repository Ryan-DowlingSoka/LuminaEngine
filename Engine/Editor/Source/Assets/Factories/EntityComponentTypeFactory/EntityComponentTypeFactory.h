#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "EntityComponentTypeFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CEntityComponentTypeFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Entity Component Type"; }
        FStringView GetDefaultAssetCreationName() override { return "NewComponentType"; }
        FString GetAssetDescription() const override { return "A data-authored ECS component type (a runtime CStruct): fields + defaults, attachable to entities."; }
        CClass* GetAssetClass() const override { return CEntityComponentType::StaticClass(); }
        FString GetCategory() const override { return "Gameplay"; }
    };
}
