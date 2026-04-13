#include "LuminaEditor.h"
#include <fstream>
#include <Core/Engine/Engine.h>
#include <Memory/Memory.h>
#include <Tools/UI/DevelopmentToolUI.h>
#include "Config/Config.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
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
    
    static void ReplaceProjectTokens(FString& Text, FStringView ProjectName)
    {
        FString Name(ProjectName.data(), ProjectName.size());

        FString NameUpper = Name;
        eastl::transform(
            NameUpper.begin(),
            NameUpper.end(),
            NameUpper.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            });

        auto ReplaceAll = [](FString& Str, const FString& From, const FString& To)
        {
            size_t Pos = 0;
            while ((Pos = Str.find(From, Pos)) != std::string::npos)
            {
                Str.replace(Pos, From.size(), To);
                Pos += To.size();
            }
        };

        ReplaceAll(Text, "$PROJECTNAMEUPPER", NameUpper);
        ReplaceAll(Text, "$PROJECTNAME", Name);
    }

    void FEditorEngine::CreateProject(FStringView NewProjectName, FStringView NewProjectPath)
    {
        FFixedString BlankProjectPath = Paths::Combine(Paths::GetEngineInstallDirectory(), "Templates", "Blank");
        
        FFixedString Combined = Paths::Combine(NewProjectPath, NewProjectName);
        std::filesystem::create_directories(Combined.c_str());
        
        for (auto& Entry : std::filesystem::recursive_directory_iterator(BlankProjectPath.c_str()))
        {
            std::filesystem::path RelativePath = std::filesystem::relative(Entry.path(), BlankProjectPath.c_str());
            
            FString RelativePathStr = RelativePath.string().c_str();
            eastl::replace(RelativePathStr.begin(), RelativePathStr.end(), '\\', '/');
            
            ReplaceProjectTokens(RelativePathStr, NewProjectName);
            FFixedString DestPath = Paths::Combine(Combined, RelativePathStr);
            
            if (Entry.is_directory())
            {
                std::filesystem::create_directories(DestPath.c_str());
            }
            else if (Entry.is_regular_file())
            {
                std::filesystem::path DestPathFS(DestPath.c_str());
                std::filesystem::create_directories(DestPathFS.parent_path());
                
                std::ifstream InputFile(Entry.path(), std::ios::binary);
                if (!InputFile.is_open())
                {
                    continue;
                }
    
                std::string FileContentStr((std::istreambuf_iterator<char>(InputFile)), std::istreambuf_iterator<char>());
                InputFile.close();
                
                FString FileContents = FileContentStr.c_str();
                ReplaceProjectTokens(FileContents, NewProjectName);
                
                std::ofstream OutputFile(DestPath.c_str(), std::ios::binary);
                if (OutputFile.is_open())
                {
                    OutputFile.write(FileContents.data(), FileContents.size());
                    OutputFile.close();
                }
            }
        }
    }
}
