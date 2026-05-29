#pragma once

#include "Assets/AssetRegistry/AssetData.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "GUID/GUID.h"

namespace Lumina
{
    class FAssetRegistry;
    struct FCookRoot;

    // One node in the cook-time reachability graph. Lives only for the
    // duration of one cook pass; not stored in the asset registry.
    struct FCookNode
    {
        FGuid                       AssetGUID;
        FFixedString                Path;          // resolved virtual path (cached)
        FName                       AssetClass;
        FName                       Chunk;         // assigned during traversal
        EAssetFlags                 EffectiveFlags = EAssetFlags::None;
    };

    // Reasons a particular asset failed to enter the graph. Surfaces in
    // the cook log so devs can fix dangling root paths and missing-dep
    // warnings before shipping.
    struct FCookGraphIssue
    {
        FString    Source;          // path or "<root>" of the failing entry
        FString    Detail;
    };

    // Build via:
    //   1. AddRoot(...) for every entry in FEngine::GetCookRoots()
    //   2. Traverse() once
    //   3. Query IsReachable / GetChunkFor / GetReachableNodes
    //
    // The graph walks FAssetData::Dependencies from the registry; it
    // never loads packages itself. Discovery + dep extraction is the
    // AssetRegistry's job, so the cook pass is cheap.
    class FCookGraph
    {
    public:
        explicit FCookGraph(const FAssetRegistry& Registry)
            : Registry(&Registry) {}

        // Add a seed. If the path resolves to a registered asset, the
        // node is added with the supplied chunk; otherwise an issue is
        // recorded and the root is skipped.
        void AddRoot(const FCookRoot& Root);

        // Convenience: bulk add.
        void AddRoots(const TVector<FCookRoot>& Roots);

        // BFS the registry's Dependencies edges. Honors EDependencyType:
        //  - Hard:       always traversed; chunk inherited from referrer
        //  - Soft:       traversed; chunk decided by the target's own roots
        //                (if also reached via Hard from another root)
        //  - Script:     same as Soft (cook-time discovery only)
        //  - Owned:      forced into referrer's chunk
        //  - EditorOnly: skipped
        //  - Generated:  skipped (runtime-only)
        void Traverse();

        // Post-Traverse queries.
        bool                IsReachable(const FGuid& A) const;
        const FCookNode*    Find(const FGuid& A) const;
        FName               GetChunkFor(const FGuid& A) const;

        // Every reached node in deterministic order (sorted by GUID),
        // so the cooker emits packages in stable order between runs.
        TVector<const FCookNode*> GetReachableNodesSorted() const;

        size_t              NumNodes() const { return Nodes.size(); }
        const TVector<FCookGraphIssue>& GetIssues() const { return Issues; }

    private:
        // Mark target reachable; if not yet present, create node and
        // enqueue for traversal. Returns true if added.
        bool VisitTarget(const FGuid& TargetGUID,
                         const FName& InheritedChunk,
                         EDependencyType EdgeType);

        const FAssetRegistry*           Registry;
        THashMap<FGuid, FCookNode, FGuidHash> Nodes;
        TVector<FGuid>                  Frontier;
        TVector<FCookGraphIssue>        Issues;
    };
}
