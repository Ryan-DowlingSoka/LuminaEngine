#include "HeaderIncludeGraph.h"

#include <EASTL/algorithm.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "Reflector/ProjectSolution.h"
#include "Reflector/ReflectionCore/ReflectedHeader.h"
#include "Reflector/ReflectionCore/ReflectedProject.h"

namespace Lumina::Reflection
{
    namespace
    {
        // Lowercase + forward slashes + absolute. Mirrors the normalisation the
        // rest of the Reflector uses for header path lookup so paths from this
        // module are directly comparable against AllHeaders entries.
        eastl::string Normalise(const std::filesystem::path& InPath)
        {
            std::error_code Ec;
            std::filesystem::path Abs = std::filesystem::weakly_canonical(InPath, Ec);
            if (Ec)
            {
                Abs = std::filesystem::absolute(InPath, Ec);
            }

            eastl::string Result = Abs.string().c_str();
            eastl::replace(Result.begin(), Result.end(), '\\', '/');
            Result.make_lower();
            return Result;
        }

        bool StartsWith(const eastl::string& Haystack, const eastl::string& Needle)
        {
            if (Needle.size() > Haystack.size())
            {
                return false;
            }
            return std::memcmp(Haystack.data(), Needle.data(), Needle.size()) == 0;
        }

        // Extract the parent directory of `Path`, normalised. Empty if the path
        // has no parent.
        eastl::string ParentDir(const eastl::string& Path)
        {
            std::filesystem::path P(Path.c_str());
            std::filesystem::path Parent = P.parent_path();
            if (Parent.empty())
            {
                return {};
            }
            return Normalise(Parent);
        }

        // Returns true for paths the cycle detector should ignore even if they
        // resolve cleanly. Today that's the `.inl` idiom: a header X.h that
        // includes X.inl which in turn includes X.h to keep IDE indexers happy.
        // This is a deliberate design pattern, not a circular dependency.
        bool IsExtensionIgnored(const eastl::string& Path)
        {
            const size_t Dot = Path.find_last_of('.');
            if (Dot == eastl::string::npos)
            {
                return false;
            }
            return Path.compare(Dot, eastl::string::npos, ".inl") == 0;
        }
    }

    bool FHeaderIncludeGraph::ScanHeader(const eastl::string& AbsPath, eastl::vector<eastl::pair<eastl::string, uint32_t>>& OutIncludes) const
    {
        std::ifstream File(AbsPath.c_str());
        if (!File.is_open())
        {
            return false;
        }

        // Match `# include "x"` allowing any whitespace between tokens. We
        // ignore `#include <...>`: those are system / external headers we
        // can't resolve into the workspace and shouldn't follow.
        static const std::regex IncludeRegex(R"(^\s*#\s*include\s*\"([^\"]+)\")");

        std::string LineBuf;
        uint32_t Line = 0;
        while (std::getline(File, LineBuf))
        {
            ++Line;

            std::smatch Match;
            if (std::regex_search(LineBuf, Match, IncludeRegex))
            {
                OutIncludes.emplace_back(eastl::pair<eastl::string, uint32_t>{ Match[1].str().c_str(), Line });
            }
        }

        return true;
    }

    eastl::string FHeaderIncludeGraph::ResolveInclude(
        const eastl::string& IncludeText,
        const eastl::string& IncluderDir,
        const eastl::vector<eastl::string>& IncludeDirs) const
    {
        // 1) Try relative to the includer (matches `#include "Sibling.h"`).
        if (!IncluderDir.empty())
        {
            std::filesystem::path Candidate = std::filesystem::path(IncluderDir.c_str()) / IncludeText.c_str();
            std::error_code Ec;
            if (std::filesystem::exists(Candidate, Ec) && !Ec)
            {
                return Normalise(Candidate);
            }
        }

        // 2) Walk the include search dirs in order, same as clang would.
        for (const eastl::string& Dir : IncludeDirs)
        {
            std::filesystem::path Candidate = std::filesystem::path(Dir.c_str()) / IncludeText.c_str();
            std::error_code Ec;
            if (std::filesystem::exists(Candidate, Ec) && !Ec)
            {
                return Normalise(Candidate);
            }
        }

        return {};
    }

    bool FHeaderIncludeGraph::IsInsideProjectRoots(const eastl::string& AbsPath) const
    {
        for (const eastl::string& Root : ProjectRoots)
        {
            if (StartsWith(AbsPath, Root))
            {
                return true;
            }
        }
        return false;
    }

    void FHeaderIncludeGraph::BuildFromWorkspace(FReflectedWorkspace* Workspace)
    {
        Nodes.clear();
        ProjectRoots.clear();
        AllIncludeDirs.clear();

        if (Workspace == nullptr)
        {
            return;
        }

        // Aggregate roots + the union of include dirs once. Resolution then has
        // a single search list to walk for any header in the graph.
        eastl::vector<eastl::string> Seeds;
        for (const auto& Project : Workspace->ReflectedProjects)
        {
            const eastl::string ProjectRoot = Normalise(std::filesystem::path(Project->Path.c_str()));
            // Trailing '/' guarantees prefix-match doesn't false-positive on
            // sibling dirs that share a prefix (e.g. "Runtime" vs "RuntimeX").
            ProjectRoots.push_back(ProjectRoot + "/");

            for (const eastl::string& Dir : Project->IncludeDirs)
            {
                eastl::string Norm = Normalise(std::filesystem::path(Dir.c_str()));
                if (eastl::find(AllIncludeDirs.begin(), AllIncludeDirs.end(), Norm) == AllIncludeDirs.end())
                {
                    AllIncludeDirs.push_back(eastl::move(Norm));
                }
            }

            for (const auto& [PathHash, Header] : Project->Headers)
            {
                Seeds.push_back(Header->HeaderPath);
            }
        }

        // BFS-ish crawl: start from every reflected header, follow its includes,
        // queue any newly-discovered file that lives inside a project root. We
        // explicitly stop at non-project paths so cycles in third-party code
        // don't get reported back to the user.
        eastl::vector<eastl::string> Frontier = Seeds;
        while (!Frontier.empty())
        {
            const eastl::string Path = Frontier.back();
            Frontier.pop_back();

            if (Nodes.find(Path) != Nodes.end())
            {
                continue;
            }

            FNode Node;
            Node.Path = Path;

            eastl::vector<eastl::pair<eastl::string, uint32_t>> RawIncludes;
            const bool bRead = ScanHeader(Path, RawIncludes);
            if (!bRead)
            {
                Nodes.emplace(Path, eastl::move(Node));
                continue;
            }

            const eastl::string IncluderDir = ParentDir(Path);

            for (const auto& [IncludeText, IncludeLine] : RawIncludes)
            {
                eastl::string Resolved = ResolveInclude(IncludeText, IncluderDir, AllIncludeDirs);
                if (Resolved.empty())
                {
                    continue;
                }
                if (!IsInsideProjectRoots(Resolved))
                {
                    continue;
                }
                if (IsExtensionIgnored(Resolved))
                {
                    // .inl files are intentionally self-referential with their
                    // owning .h. Skip the edge so the idiom doesn't surface as
                    // a false-positive cycle.
                    continue;
                }

                Node.Includes.push_back({ Resolved, IncludeLine });

                if (Nodes.find(Resolved) == Nodes.end())
                {
                    Frontier.push_back(Resolved);
                }
            }

            Nodes.emplace(Path, eastl::move(Node));
        }
    }

    uint32_t FHeaderIncludeGraph::GetIncludeLine(const eastl::string& IncluderPath, const eastl::string& IncludeePath) const
    {
        const auto It = Nodes.find(IncluderPath);
        if (It == Nodes.end())
        {
            return 0;
        }
        for (const FResolvedInclude& Inc : It->second.Includes)
        {
            if (Inc.Path == IncludeePath)
            {
                return Inc.Line;
            }
        }
        return 0;
    }

    eastl::vector<FHeaderCycle> FHeaderIncludeGraph::DetectCycles() const
    {
        // Standard three-colour DFS. White = unvisited, Grey = on the active
        // recursion stack, Black = fully explored. Hitting a Grey node means
        // we've found a cycle and the active stack contains its members.
        enum class EColor : uint8_t { White, Grey, Black };

        eastl::hash_map<eastl::string, EColor> Colour;
        Colour.reserve(Nodes.size());
        for (const auto& [Path, _] : Nodes)
        {
            Colour.emplace(Path, EColor::White);
        }

        eastl::vector<eastl::string> Stack;
        eastl::vector<FHeaderCycle> Cycles;
        eastl::hash_map<eastl::string, bool> Reported; // dedup key

        // Iterative DFS to avoid blowing the C++ stack on giant graphs.
        struct FFrame
        {
            const FNode* Node;
            size_t       NextEdge;
        };

        for (const auto& [StartPath, _] : Nodes)
        {
            if (Colour[StartPath] != EColor::White)
            {
                continue;
            }

            const auto StartIt = Nodes.find(StartPath);
            if (StartIt == Nodes.end())
            {
                continue;
            }

            eastl::vector<FFrame> Frames;
            Frames.push_back({ &StartIt->second, 0 });
            Colour[StartPath] = EColor::Grey;
            Stack.push_back(StartPath);

            while (!Frames.empty())
            {
                FFrame& Top = Frames.back();
                if (Top.NextEdge >= Top.Node->Includes.size())
                {
                    Colour[Top.Node->Path] = EColor::Black;
                    Stack.pop_back();
                    Frames.pop_back();
                    continue;
                }

                const eastl::string& Target = Top.Node->Includes[Top.NextEdge++].Path;

                auto ColourIt = Colour.find(Target);
                if (ColourIt == Colour.end())
                {
                    continue;
                }

                if (ColourIt->second == EColor::Black)
                {
                    continue;
                }

                if (ColourIt->second == EColor::Grey)
                {
                    // Walk the active stack back to find where Target first
                    // appeared -- that index marks the start of the cycle.
                    auto StackIt = eastl::find(Stack.begin(), Stack.end(), Target);
                    if (StackIt == Stack.end())
                    {
                        continue;
                    }

                    FHeaderCycle Cycle(StackIt, Stack.end());
                    Cycle.push_back(Target);   // close the loop: last == first

                    // Canonicalise: rotate so the lexicographically smallest
                    // path is first. Two different start nodes that walked the
                    // same loop now produce identical keys.
                    auto MinIt = eastl::min_element(Cycle.begin(), Cycle.end() - 1);
                    eastl::vector<eastl::string> Canonical;
                    Canonical.reserve(Cycle.size());
                    Canonical.insert(Canonical.end(), MinIt, Cycle.end() - 1);
                    Canonical.insert(Canonical.end(), Cycle.begin(), MinIt);
                    Canonical.push_back(*MinIt);

                    eastl::string Key;
                    for (const eastl::string& Step : Canonical)
                    {
                        Key += Step;
                        Key += '|';
                    }

                    if (Reported.find(Key) == Reported.end())
                    {
                        Reported.emplace(Key, true);
                        Cycles.push_back(eastl::move(Canonical));
                    }
                    continue;
                }

                // White -- descend into Target.
                const auto TargetNodeIt = Nodes.find(Target);
                if (TargetNodeIt == Nodes.end())
                {
                    continue;
                }

                Colour[Target] = EColor::Grey;
                Stack.push_back(Target);
                Frames.push_back({ &TargetNodeIt->second, 0 });
            }
        }

        return Cycles;
    }
}
