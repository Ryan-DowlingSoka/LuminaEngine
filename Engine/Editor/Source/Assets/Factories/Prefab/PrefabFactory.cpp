#include "pch.h"
#include "PrefabFactory.h"

#include "Assets/AssetTypes/Prefabs/Prefab.h"

namespace Lumina
{
    CClass* CPrefabFactory::GetAssetClass() const
    {
        return CPrefab::StaticClass();
    }

    CObject* CPrefabFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        return NewObject<CPrefab>(Package, Name);
    }
}
