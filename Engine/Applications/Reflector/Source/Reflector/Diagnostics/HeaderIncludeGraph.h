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

    // Directed #include graph across a workspace's headers, walked for cycles via a lightweight
    // line-based regex (not a full preprocessor): quoted includes only, ignores #if guards, stops at project roots.
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

        // Seeds from every reflected header and recursively walks includes resolving inside a project root.
        void BuildFromWorkspace(FReflectedWorkspace* Workspace);

        // One entry per cycle, deduplicated by canonical (lexicographically minimum) rotation.
        eastl::vector<FHeaderCycle> DetectCycles() const;

        // Line in IncluderPath that #includes IncludeePath, or 0 if no direct edge exists.
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

        // True when AbsPath sits beneath a project root; keeps the crawl bounded to the workspace.
        bool IsInsideProjectRoots(const eastl::string& AbsPath) const;

        eastl::hash_map<eastl::string, FNode>   Nodes;
        eastl::vector<eastl::string>            ProjectRoots;       // normalised lowercase prefixes
        eastl::vector<eastl::string>            AllIncludeDirs;     // union across projects
    };
}
