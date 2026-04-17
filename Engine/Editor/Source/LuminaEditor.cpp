#include "LuminaEditor.h"
#include <fstream>
#include <Core/Engine/Engine.h>
#include <Memory/Memory.h>
#include <Tools/UI/DevelopmentToolUI.h>
#include "Config/Config.h"
#include "FileSystem/FileSystem.h"
#include "GUID/GUID.h"
#include "Paths/Paths.h"
#include "Platform/Process/PlatformProcess.h"
#include "UI/EditorUI.h"
#include "World/WorldManager.h"

namespace Lumina
{
    EDITOR_API FEditorEngine* GEditorEngine = nullptr;
    
    bool FEditorEngine::Init()
    {
        VFS::Mount<VFS::FNativeFileSystem>("/Editor", Paths::Combine(Paths::GetEngineDirectory(), "Editor"));
        
        GConfig->LoadPath("/Editor/Config");
        
        bool bSuccess = FEngine::Init();
        
        entt::locator<entt::meta_ctx>::reset(GetEngineMetaService());

        return bSuccess;
    }

    bool FEditorEngine::Shutdown()
    {
        return FEngine::Shutdown();
    }

    CWorld* FEditorEngine::GetCurrentEditorWorld() const
    {
        return nullptr;
    }

    IDevelopmentToolUI* FEditorEngine::CreateDevelopmentTools()
    {
        return Memory::New<FEditorUI>();
    }
    
    struct FProjectTemplateContext
    {
        FString Name;
        FString NameUpper;
        FString Guid;
        FString Description;
    };

    static void ReplaceProjectTokens(FString& Text, const FProjectTemplateContext& Ctx)
    {
        auto ReplaceAll = [](FString& Str, const FString& From, const FString& To)
        {
            if (From.empty())
            {
                return;
            }
            size_t Pos = 0;
            while ((Pos = Str.find(From, Pos)) != FString::npos)
            {
                Str.replace(Pos, From.size(), To);
                Pos += To.size();
            }
        };

        // Longer tokens must be replaced first so that $PROJECTNAME doesn't
        // eat the prefix of $PROJECTNAMEUPPER.
        ReplaceAll(Text, "$PROJECTNAMEUPPER", Ctx.NameUpper);
        ReplaceAll(Text, "$PROJECTDESCRIPTION", Ctx.Description);
        ReplaceAll(Text, "$PROJECTGUID", Ctx.Guid);
        ReplaceAll(Text, "$PROJECTNAME", Ctx.Name);
    }

    void FEditorEngine::CreateProject(FStringView NewProjectName, FStringView NewProjectPath)
    {
        FProjectTemplateContext Ctx;
        Ctx.Name.assign(NewProjectName.data(), NewProjectName.size());
        Ctx.NameUpper = Ctx.Name;
        eastl::transform(
            Ctx.NameUpper.begin(),
            Ctx.NameUpper.end(),
            Ctx.NameUpper.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            });
        Ctx.Guid = FGuid::New().ToString(true, true);
        Ctx.Description = "A Lumina game project";

        FFixedString BlankProjectPath = Paths::Combine(Paths::GetEngineInstallDirectory(), "Templates", "Blank");

        FFixedString Combined = Paths::Combine(NewProjectPath, NewProjectName);
        std::filesystem::create_directories(Combined.c_str());
        
        for (auto& Entry : std::filesystem::recursive_directory_iterator(BlankProjectPath.c_str()))
        {
            std::filesystem::path RelativePath = std::filesystem::relative(Entry.path(), BlankProjectPath.c_str());

            FString RelativePathStr = RelativePath.string().c_str();
            eastl::replace(RelativePathStr.begin(), RelativePathStr.end(), '\\', '/');

            ReplaceProjectTokens(RelativePathStr, Ctx);
            FFixedString DestPath = Paths::Combine(Combined, RelativePathStr);

            if (Entry.is_directory())
            {
                std::filesystem::create_directories(DestPath.c_str());
            }
            else if (Entry.is_regular_file())
            {
                std::filesystem::path DestPathFS(DestPath.c_str());
                std::filesystem::create_directories(DestPathFS.parent_path());

                const std::filesystem::path SourcePath = Entry.path();
                const std::string Ext = SourcePath.extension().string();

                // Binary files (e.g. premake5.exe) must be copied verbatim.
                // Only run token replacement on text files.
                const bool bIsTextFile =
                    Ext == ".h"     || Ext == ".hpp"        || Ext == ".cpp"    ||
                    Ext == ".c"     || Ext == ".inl"        || Ext == ".lua"    ||
                    Ext == ".json"  || Ext == ".lproject"   || Ext == ".luau"   ||
                    Ext == ".bat"   || Ext == ".py"         || Ext == ".md"     ||
                    Ext == ".txt";

                if (!bIsTextFile)
                {
                    std::filesystem::copy_file(
                        SourcePath,
                        DestPath.c_str(),
                        std::filesystem::copy_options::overwrite_existing);
                    continue;
                }

                std::ifstream InputFile(SourcePath, std::ios::binary);
                if (!InputFile.is_open())
                {
                    continue;
                }

                std::string FileContentStr((std::istreambuf_iterator<char>(InputFile)), std::istreambuf_iterator<char>());
                InputFile.close();

                FString FileContents = FileContentStr.c_str();
                ReplaceProjectTokens(FileContents, Ctx);

                std::ofstream OutputFile(DestPath.c_str(), std::ios::binary);
                if (OutputFile.is_open())
                {
                    OutputFile.write(FileContents.data(), FileContents.size());
                    OutputFile.close();
                }
            }
        }
        
        Platform::LaunchURL(UTF8_TO_TCHAR(Combined.c_str()));
        
    }
}
