#include <fstream>
#include <print>
#include "StringHash.h"
#include "nlohmann/json.hpp"
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

    FClangParser Parser;
    bool bParseResult = Parser.Parse(&Workspace);

    if (!bParseResult)
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
