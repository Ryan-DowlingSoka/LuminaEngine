#include "ClangParser.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <clang-c/Index.h>
#include "EASTL/fixed_vector.h"
#include "Reflector/ProjectSolution.h"
#include "Reflector/ReflectionCore/ReflectedProject.h"
#include "Visitors/ClangTranslationUnit.h"



namespace Lumina::Reflection
{
    bool FClangParser::Parse(FReflectedWorkspace* Workspace)
    {
        CXTranslationUnit TranslationUnit = nullptr;
        CXIndex ClangIndex = nullptr;
    
        ParsingContext.Workspace = Workspace;
        
        const eastl::string AmalgamationPath = std::filesystem::absolute("ReflectHeaders.gen.h").string().c_str();

        std::ofstream AmalgamationFile(AmalgamationPath.c_str());
        if (!AmalgamationFile.is_open())
        {
            spdlog::error("Failed to create amalgamation file");
            return false;
        }
        AmalgamationFile << "#pragma once\n\n";
        
        // Needed to keep dynamic args alive.
        eastl::fixed_vector<eastl::string, 256, false>  ClangArgStorage;
        eastl::fixed_vector<const char*, 256, false>    ClangArgs;
        
        auto AppendArg = [&](eastl::string Arg)
        {
            ClangArgStorage.emplace_back(eastl::move(Arg));
            ClangArgs.emplace_back(ClangArgStorage.back().c_str());
        };
        
        eastl::string LuminaDirectory = std::getenv("LUMINA_DIR");
        if (!LuminaDirectory.empty() && LuminaDirectory.back() == '/' )
        {
            LuminaDirectory.pop_back();
        }
        
        for (const auto& Project : Workspace->ReflectedProjects)
        {
            eastl::string APIDecl = "-D" + Project->Name + "_API=";
            APIDecl.make_upper();
            
            AppendArg(eastl::move(APIDecl));
            
            for (const eastl::string& IncludeDir : Project->IncludeDirs)
            {
                if (IncludeDir.find("GLM") != eastl::string::npos || IncludeDir.find("glm") != eastl::string::npos)
                {
                    continue;
                }
                
                AppendArg("-I" + IncludeDir);
            }
            
            for (auto& [Path, Header] : Project->Headers)
            {
                AmalgamationFile << "#include \"" << Path.c_str() << "\"\n";
                ParsingContext.AllHeaders.emplace(Path, Header.get());
                // @TODO For some reason enabling this breaks the manualreflecttypes.
                //ClangArgs.emplace_back("-include");
                //ClangArgs.emplace_back(Path.c_str());
                ParsingContext.NumHeadersReflected++;
            }
        }
    
        AmalgamationFile.close();   
        AppendArg("-includeCore/Object/ManualReflectTypes.h");
        AppendArg("-x");
        AppendArg("c++");
        AppendArg("-std=c++23");
        AppendArg("-O0");
        AppendArg("-DREFLECTION_PARSER");
        AppendArg("-DNDEBUG");
        AppendArg("-fms-extensions");
        AppendArg("-fms-compatibility");
        AppendArg("-Wfatal-errors=0");
        AppendArg("-ferror-limit=1000000000");
        AppendArg("-Wno-multichar");
        AppendArg("-Wno-deprecated-builtins");
        AppendArg("-Wno-unknown-warning-option");
        AppendArg("-Wno-return-type-c-linkage");
        AppendArg("-Wno-c++98-compat-pedantic");
        AppendArg("-Wno-gnu-folding-constant");
        AppendArg("-Wno-vla-extension-static-assert");
        AppendArg("-fno-spell-checking");
        AppendArg("-fno-delayed-template-parsing");
    
        ClangIndex = clang_createIndex(0, 0);
        
        constexpr uint32_t ClangOptions = 
            CXTranslationUnit_DetailedPreprocessingRecord |
            CXTranslationUnit_SkipFunctionBodies | 
            CXTranslationUnit_CacheCompletionResults |
            CXTranslationUnit_IncludeBriefCommentsInCodeCompletion |
            CXTranslationUnit_KeepGoing;
        
        CXErrorCode Result = clang_parseTranslationUnit2(
            ClangIndex,
            AmalgamationPath.c_str(),
            ClangArgs.data(),
            (int)ClangArgs.size(),
            nullptr,
            0,
            ClangOptions,
            &TranslationUnit);
        
        CXCursor Cursor = clang_getTranslationUnitCursor(TranslationUnit);
        if (clang_visitChildren(Cursor, VisitTranslationUnit, &ParsingContext) != 0)
        {
            spdlog::error("A problem occured during translation unit parsing");
        }
        
        if (Result != CXError_Success)
        {
            switch (Result)
            {
            case CXError_Failure:
                spdlog::error("Clang Unknown failure");
                break;
    
            case CXError_Crashed:
                spdlog::error("Clang crashed");
                break;
    
            case CXError_InvalidArguments:
                spdlog::error("Clang Invalid arguments");
                break;
    
            case CXError_ASTReadError:
                spdlog::error("Clang AST read error");
                break;
            }
        }
        
        std::filesystem::remove(AmalgamationPath.c_str());
        clang_disposeIndex(ClangIndex);
        return Result == CXError_Success;
    }
}
