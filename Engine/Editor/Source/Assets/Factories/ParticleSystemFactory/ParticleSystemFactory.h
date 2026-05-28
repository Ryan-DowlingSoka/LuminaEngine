#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "ParticleSystemFactory.generated.h"

namespace Lumina
{
	REFLECT()
	class CParticleSystemFactory : public CFactory
	{
		GENERATED_BODY()
	public:

		CObject* CreateNew(const FName& Name, CPackage* Package) override;

		FString GetAssetName() const override { return "Particle System"; }
		FStringView GetDefaultAssetCreationName() override { return "NewParticleSystem"; }
		FString GetAssetDescription() const override { return "A GPU particle system."; }
		CClass* GetAssetClass() const override { return CParticleSystem::StaticClass(); }
		FString GetCategory() const override { return "Effects"; }
	};
}
