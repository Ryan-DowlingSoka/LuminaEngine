#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/Name.h"
#include "Core/Math/Transform.h"
#include "entt/entt.hpp"

namespace Lumina
{
    class CWorld;
    class CObject;

    // Spawns/applies a dropped asset. DropTarget = entt::null for a viewport drop, else the outliner
    // row's entity. Returns the affected entity (created or DropTarget); entt::null on failure.
    using FEditorAssetDropHandler = TFunction<entt::entity(CWorld* World, CObject* Asset, const FTransform& SpawnTransform, entt::entity DropTarget)>;

    class FEditorAssetDropRegistry
    {
    public:

        static FEditorAssetDropRegistry& Get();

        void Register(FName AssetClass, FEditorAssetDropHandler Handler);

        const FEditorAssetDropHandler* FindHandler(FName AssetClass) const;

    private:

        THashMap<FName, FEditorAssetDropHandler> Handlers;
    };
}
