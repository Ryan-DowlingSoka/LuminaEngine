#pragma once

#include "Assets/Factories/Factory.h"
#include "Assets/AssetTypes/GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionFactory.generated.h"

namespace Lumina
{
    REFLECT()
    class CGeometryCollectionFactory : public CFactory
    {
        GENERATED_BODY()
    public:

        CObject* CreateNew(const FName& Name, CPackage* Package) override;

        FString GetAssetName() const override { return "Geometry Collection"; }
        FStringView GetDefaultAssetCreationName() override { return "NewGeometryCollection"; }
        FString GetAssetDescription() const override { return "Pre-fractured geometry: a mesh broken into convex chunks a destructible shatters into. Open it and pick a source mesh to bake."; }
        CClass* GetAssetClass() const override { return CGeometryCollection::StaticClass(); }
    };
}
