#include <fstream>
#include <print>
#include "StringHash.h"
#include "nlohmann/json.hpp"
#include "Reflector/Diagnostics/HeaderIncludeGraph.h"
#include "Reflector/Diagnostics/LRTDiagnostics.h"
#include "Reflector/ProjectSolution.h"
#include "Reflector/Clang/ClangParser.h"
#include "Reflector/CodeGeneration/CodeGenerator.h"
#include "Reflector/ReflectionCore/ReflectedProject.h"
#include <spdlog/spdlog.h>


using json = nlohmann::json;
using namespace Lumina::Reflection;

int main(int argc, char* argv[])
{    
    Lumina::FStringHash::Initialize();
    
    spdlog::info("===============================================");
    spdlog::info("======== Lumina Reflection Tool (LRT) =========");
    spdlog::info("===============================================");
    
#if 0
    
    eastl::string InputFile = "H:/LuminaEngine/Reflection_Files.json";
    
#else
    if (argc < 2)
    {
        spdlog::error("Missing command line argument, please specify a json file.");
        return 1;
    }
    
    eastl::string InputFile = argv[1];
#endif
    
    std::ifstream File(InputFile.c_str());
    if (!File.is_open())
    {
        spdlog::error("Failed to open file {}", InputFile.c_str());
        return 1; 
    }
    
    
    json Data = json::parse(File);
    
    eastl::string WorkspaceName     = Data["WorkspaceName"].get<std::string>().c_str();
    eastl::string WorkspacePath     = Data["WorkspacePath"].get<std::string>().c_str();
    
    FReflectedWorkspace Workspace(WorkspacePath.c_str());
    
    for (const auto& Project : Data["Projects"])
    {
        eastl::string ProjectName = Project["Name"].get<std::string>().c_str();
        eastl::string ProjectPath = Project["Path"].get<std::string>().c_str();
        
        auto ReflectedProject = eastl::make_unique<FReflectedProject>(&Workspace);
        ReflectedProject->Name = eastl::move(ProjectName);
        ReflectedProject->Path = eastl::move(ProjectPath);
        
        for (const auto& IncludeDirJson : Project["IncludeDirs"])
        {
            eastl::string IncludeDir = IncludeDirJson.get<std::string>().c_str();
            ReflectedProject->IncludeDirs.push_back(eastl::move(IncludeDir));
        }
        
        for (const auto& ProjectFileJson : Project["Files"])
        {
            eastl::string ProjectFile = ProjectFileJson.get<std::string>().c_str();
            ProjectFile.make_lower();
            eastl::replace(ProjectFile.begin(), ProjectFile.end(), '\\', '/');
            
            auto ReflectedHeader = eastl::make_unique<FReflectedHeader>(ReflectedProject.get(), ProjectFile);

            Lumina::FStringHash HeaderHash(ProjectFile);
            ReflectedProject->Headers.emplace(HeaderHash, eastl::move(ReflectedHeader));
        }
        
        Workspace.AddReflectedProject(eastl::move(ReflectedProject));
    }

    // Static include-graph pass. We run this BEFORE handing the workspace to
    // libclang because cycles can produce confusing downstream parse errors
    // (forward declarations resolving in the wrong order, ambiguous names),
    // and surfacing them up front gives the user a clean LRT error to act on.
    {
        FHeaderIncludeGraph Graph;
        Graph.BuildFromWorkspace(&Workspace);

        const auto Cycles = Graph.DetectCycles();
        for (const FHeaderCycle& Cycle : Cycles)
        {
            // Build a "A.h -> B.h -> A.h" arrow chain for the message body.
            eastl::string Arrow;
            for (size_t i = 0; i < Cycle.size(); ++i)
            {
                if (i > 0)
                {
                    Arrow += " -> ";
                }
                Arrow += Cycle[i];
            }

            // Anchor the diagnostic at the first include edge of the cycle so
            // double-clicking the error in the build log opens the source line
            // that triggers the loop.
            FDiagLocation Loc;
            Loc.File = Cycle.front();
            if (Cycle.size() >= 2)
            {
                Loc.Line = Graph.GetIncludeLine(Cycle[0], Cycle[1]);
            }

            FDiagnostics::Get().Errorf(Loc, EDiagId::CircularHeaderInclude,
                "Circular header include: %s", Arrow.c_str());
        }

        if (!Cycles.empty())
        {
            // Bail before parsing -- clang would otherwise spend tens of
            // seconds chewing on a workspace that has a structural defect.
            FDiagnostics::Get().PrintSummary();
            return 1;
        }
    }

    FClangParser Parser;
    bool bParseResult = Parser.Parse(&Workspace);

    if (!bParseResult)
    {
        FDiagnostics::Get().PrintSummary();
        return 1;
    }

    // Per-header include validation: any header that contained a reflection
    // macro must end its include block with `<stem>.generated.h`. Catches
    // the three classic misconfigurations:
    //   - forgot to include the generated header at all
    //   - included it but tucked another #include after it (which usually
    //     hides definitions emitted by the generated header from later
    //     includes — consistent ordering matters)
    //   - copy-pasted a different file's `.generated.h` (Bar.h includes
    //     Foo.generated.h)
    for (const auto& Project : Workspace.ReflectedProjects)
    {
        for (auto& [_, Header] : Project->Headers)
        {
            if (!Header->bHasReflectionMacros)
            {
                continue;
            }

            // ManualReflectTypes.h is force-included by ClangParser as the
            // canonical home for reflected glm types and the like. It is not
            // itself part of the codegen output flow and has no companion
            // generated.h.
            if (Header->HeaderPath.find("manualreflecttypes") != eastl::string::npos)
            {
                continue;
            }

            eastl::string ExpectedGenerated = Header->FileName + ".generated.h";
            ExpectedGenerated.make_lower();

            const FIncludeRef* GeneratedInclude = nullptr;
            const FIncludeRef* WrongGenerated   = nullptr;

            constexpr const char* kGeneratedSuffix = ".generated.h";
            constexpr size_t      kGeneratedSuffixLen = 12;

            for (const FIncludeRef& Inc : Header->Includes)
            {
                const bool bEndsWithGen = Inc.Basename.size() >= kGeneratedSuffixLen &&
                    Inc.Basename.compare(Inc.Basename.size() - kGeneratedSuffixLen, kGeneratedSuffixLen, kGeneratedSuffix) == 0;
                if (!bEndsWithGen)
                {
                    continue;
                }

                if (Inc.Basename == ExpectedGenerated)
                {
                    GeneratedInclude = &Inc;
                }
                else if (WrongGenerated == nullptr)
                {
                    WrongGenerated = &Inc;
                }
            }

            FDiagLocation HeaderLoc;
            HeaderLoc.File = Header->HeaderPath;

            if (GeneratedInclude == nullptr)
            {
                if (WrongGenerated != nullptr)
                {
                    FDiagLocation Loc = HeaderLoc;
                    Loc.Line = WrongGenerated->LineNumber;
                    FDiagnostics::Get().Errorf(Loc, EDiagId::WrongGeneratedHeader,
                        "Header includes '%s' but reflection expects '%s'. "
                        "The generated header name must match the source filename stem.",
                        WrongGenerated->Spelling.c_str(), ExpectedGenerated.c_str());
                }
                else
                {
                    HeaderLoc.Line = 1;
                    FDiagnostics::Get().Errorf(HeaderLoc, EDiagId::MissingGeneratedHeader,
                        "Header uses REFLECT/GENERATED_BODY/PROPERTY/FUNCTION but does not "
                        "#include \"%s\". Add it as the last include in the file.",
                        ExpectedGenerated.c_str());
                }
                continue;
            }

            // Confirmed the right generated.h is included; now verify it's
            // the last include in the file.
            const FIncludeRef* LaterInclude = nullptr;
            for (const FIncludeRef& Inc : Header->Includes)
            {
                if (Inc.LineNumber > GeneratedInclude->LineNumber && LaterInclude == nullptr)
                {
                    LaterInclude = &Inc;
                }
            }

            if (LaterInclude != nullptr)
            {
                FDiagLocation Loc = HeaderLoc;
                Loc.Line = LaterInclude->LineNumber;
                FDiagnostics::Get().Errorf(Loc, EDiagId::GeneratedHeaderNotLast,
                    "'%s' must be the last #include in this header, but '%s' follows it.",
                    ExpectedGenerated.c_str(), LaterInclude->Spelling.c_str());
            }
        }
    }

    if (FDiagnostics::Get().GetErrorCount() != 0)
    {
        FDiagnostics::Get().PrintSummary();
        return 1;
    }

    FCodeGenerator CodeGenerator(&Workspace, Parser.ParsingContext.ReflectionDatabase);

    CodeGenerator.GenerateCode();

    Lumina::FStringHash::Shutdown();

    // If any LRT diagnostics fired we surface a non-zero exit so the build
    // halts; the diagnostic lines themselves were already printed when emitted.
    FDiagnostics::Get().PrintSummary();
    return FDiagnostics::Get().GetErrorCount() == 0 ? 0 : 1;
}
