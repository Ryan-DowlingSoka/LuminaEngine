#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace Lumina::Reflection
{
    class FReflectedWorkspace;

    // A cycle is the chain of headers a -> b -> ... -> a, where the first and
    // last entries are the same path.
    using FHeaderCycle = eastl::vector<eastl::string>;

    /**
     * Builds a directed graph of #include relationships across the headers in
     * a reflected workspace, then walks it for cycles.
     *
     * The scanner is intentionally lightweight -- it does a line-based regex
     * pass instead of running a full preprocessor. Trade-offs:
     *   - Reliable for the common forms `#include "X"` and `#include <X>`.
     *   - Sees all #includes regardless of #if/#ifdef guards. This means a
     *     header that #includes itself only inside `#if 0` is still flagged.
     *     The fix is the same in either case (rewire the include), so this is
     *     a worthwhile false-positive trade for not pulling clang in here.
     *   - Only follows quoted includes; angle-bracket / system headers are
     *     never part of the graph (we'd have no chance of resolving them and
     *     they're not the user's to rearrange anyway).
     *   - Stops crawling at any path that resolves outside the project roots
     *     (External/, ThirdParty/, system dirs) -- their cycles aren't ours.
     */
    class FHeaderIncludeGraph
    {
    public:

        struct FResolvedInclude
        {
            eastl::string Path;     // normalised absolute path (lowercase, '/').
            uint32_t      Line = 0; // 1-based line in the includer.
        };

        struct FNode
        {
            eastl::string                   Path;       // owning normalised path
            eastl::vector<FResolvedInclude> Includes;   // outgoing edges
        };

        // Seeds the graph with every reflected header in the workspace and
        // recursively walks their includes that resolve inside any project
        // root. Safe to call once per Reflector invocation.
        void BuildFromWorkspace(FReflectedWorkspace* Workspace);

        // Returns one entry per detected cycle. Cycles are deduplicated by
        // their canonical (lexicographically minimum) rotation, so the same
        // loop reached via different start nodes is reported once.
        eastl::vector<FHeaderCycle> DetectCycles() const;

        // Returns the line in IncluderPath that #includes IncludeePath, or 0
        // if no such direct edge exists. Used to point the diagnostic at the
        // exact source line of the offending include.
        uint32_t GetIncludeLine(const eastl::string& IncluderPath, const eastl::string& IncludeePath) const;

    private:

        // Pulls the textual #include "..." directives out of a single header.
        // Returns false if the file couldn't be opened.
        bool ScanHeader(const eastl::string& AbsPath, eastl::vector<eastl::pair<eastl::string, uint32_t>>& OutIncludes) const;

        // Tries IncluderDir/Text first, then each include search dir. Returns
        // an empty string when nothing resolves to an existing file.
        eastl::string ResolveInclude(
            const eastl::string& IncludeText,
            const eastl::string& IncluderDir,
            const eastl::vector<eastl::string>& IncludeDirs) const;

        // True when AbsPath sits beneath any of the project root directories
        // we're allowed to recurse into. Keeps the crawl bounded to the
        // workspace.
        bool IsInsideProjectRoots(const eastl::string& AbsPath) const;

        eastl::hash_map<eastl::string, FNode>   Nodes;
        eastl::vector<eastl::string>            ProjectRoots;       // normalised lowercase prefixes
        eastl::vector<eastl::string>            AllIncludeDirs;     // union across projects
    };
}
