#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "AnimationGraphFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CAnimationGraphFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Animation Graph"; }
        FStringView GetDefaultAssetCreationName() override { return "NewAnimationGraph"; }
        FString GetAssetDescription() const override { return "An animation graph."; }
        CClass* GetAssetClass() const override { return CAnimationGraph::StaticClass(); }

    };
}
