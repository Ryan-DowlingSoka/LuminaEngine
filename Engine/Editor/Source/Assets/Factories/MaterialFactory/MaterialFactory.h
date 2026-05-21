#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "MaterialFactory.generated.h"


namespace Lumina
{
	REFLECT()
	class CMaterialFactory : public CFactory
	{
		GENERATED_BODY()
	public:

		CObject* CreateNew(const FName& Name, CPackage* Package) override;

		FString GetAssetName() const override { return "Material"; }
		FStringView GetDefaultAssetCreationName() override { return "NewMaterial"; }
		FString GetAssetDescription() const override { return "A material."; }
		CClass* GetAssetClass() const override { return CMaterial::StaticClass(); }

	};
}
