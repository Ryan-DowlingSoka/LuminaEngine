#pragma once

#include "Containers/Name.h"
#include "Containers/String.h"

namespace Lumina
{
    // One declared entry point into the cook reachability graph. The
    // cooker BFS's outward from every CookRoot; any asset NOT reachable
    // from at least one root is not shipped. Sourced from:
    //  - The loaded .lproject's "CookRoots" array
    //  - Each enabled plugin's .lplugin "CookRoots" array
    //  - Legacy: a single auto-generated root for Project.GameStartupMap
    //    (kept as migration shim; new projects should use CookRoots[]).
    struct FCookRoot
    {
        // VFS path to the asset (e.g. "/Game/Maps/MainMenu.lasset" or
        // bare "/Game/Maps/MainMenu" -- resolver handles either form).
        FString Asset;

        // Chunk name the asset (and its hard-reachable children) land in.
        // Phase 1 ships one PAK so this is recorded but not yet routed;
        // Phase 3 uses it for per-chunk .lpak emission.
        FName   Chunk;
    };
}
