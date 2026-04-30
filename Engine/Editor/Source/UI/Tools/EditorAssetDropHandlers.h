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

    // Spawns or applies an asset to a world. Called when a content-browser tile is dropped on a
    // viewport (DropTarget = entt::null) or on an outliner row (DropTarget = that row's entity).
    // Returns the affected entity (newly created or DropTarget); entt::null on failure.
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
