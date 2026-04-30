#include "LuminaEditor.h"
#include <filesystem>
#include <fstream>
#include <Core/Engine/Engine.h>
#include <Core/Engine/EngineMetaContext.h>
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
    
    static void RegisterEditorSettings()
    {
        const FStringView EditorFile = "/Editor/Config/EditorSettings.json";

        // World editor — gizmo snapping
        GConfig->RegisterSetting(FConfigSetting::Make("Editor.WorldEditorTool.GuizmoSnapEnabled", EConfigValueType::Bool)
            .WithCategory("Editor/World Tool")
            .WithDescription("Whether transform gizmo snapping is enabled by default")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.WorldEditorTool.GuizmoSnapTranslate", EConfigValueType::Float)
            .WithCategory("Editor/World Tool")
            .WithDescription("Snap step (units) for the translate gizmo")
            .WithDefault(0.1f)
            .WithRange(0.001, 100.0)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.WorldEditorTool.GuizmoSnapRotate", EConfigValueType::Float)
            .WithCategory("Editor/World Tool")
            .WithDescription("Snap step (degrees) for the rotate gizmo")
            .WithDefault(5.0f)
            .WithRange(0.1, 90.0)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.WorldEditorTool.GuizmoSnapScale", EConfigValueType::Float)
            .WithCategory("Editor/World Tool")
            .WithDescription("Snap step (scale factor) for the scale gizmo")
            .WithDefault(0.1f)
            .WithRange(0.001, 10.0)
            .WithOwnerFile(EditorFile));

        // Content browser
        GConfig->RegisterSetting(FConfigSetting::Make("Editor.ContentBrowser.TileSize", EConfigValueType::Float)
            .WithCategory("Editor/Content Browser")
            .WithDescription("Pixel size of asset tiles in the content browser")
            .WithDefault(86.0f)
            .WithRange(32.0, 256.0)
            .WithOwnerFile(EditorFile));

        // Recents — typed as a string list of project names
        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RecentProjects", EConfigValueType::StringArray)
            .WithCategory("Editor/General")
            .WithDescription("Recently opened projects (most recent appended last)")
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.StartupProject", EConfigValueType::String)
            .WithCategory("Editor/General")
            .WithDescription("Project to load automatically on editor launch (\"NULL\" to disable)")
            .WithDefault(std::string("NULL"))
            .WithOwnerFile(EditorFile));

        // Lua editor — open in-engine by default; flip this to route .lua/.luau
        // double-clicks to the OS-registered handler instead.
        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.UsePlatformEditor", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Open .lua/.luau files with the OS default editor instead of the in-engine Lua editor")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.FontScale", EConfigValueType::Float)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Font scale multiplier for the in-engine Lua editor")
            .WithDefault(1.25f)
            .WithRange(0.75, 3.0)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.TabSize", EConfigValueType::Int)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Tab size in spaces")
            .WithDefault(4)
            .WithRange(1, 8)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.LineSpacing", EConfigValueType::Float)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Line spacing multiplier")
            .WithDefault(1.0f)
            .WithRange(1.0, 2.0)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.ShowWhitespace", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Render whitespace glyphs (spaces and tabs)")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.ShowLineNumbers", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Show line numbers in the gutter")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.AutoIndent", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Auto-indent new lines based on surrounding scope")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.MatchBrackets", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Highlight matching brackets at the cursor")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.CompletePairs", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Auto-close paired glyphs (parentheses, brackets, quotes)")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.Palette", EConfigValueType::String)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Color palette for the Lua editor (\"Dark\" or \"Light\")")
            .WithDefault(std::string("Dark"))
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.ShowMiniMap", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Show the scrollbar mini-map")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.InsertSpacesOnTabs", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Insert spaces when the user presses Tab instead of a tab character")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.TrimTrailingOnSave", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Strip trailing whitespace from every line on save")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.AutoTriggerCompletion", EConfigValueType::Bool)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Open the autocomplete popup automatically while typing identifiers")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.LuaEditor.AutoTriggerDelayMs", EConfigValueType::Int)
            .WithCategory("Editor/Lua Editor")
            .WithDescription("Delay (ms) between the last keystroke and the autocomplete popup")
            .WithDefault(100)
            .WithRange(0, 1000)
            .WithOwnerFile(EditorFile));

        // RmlUi editor settings.
        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.FontScale", EConfigValueType::Float)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Font scale multiplier for the in-engine RmlUi editor")
            .WithDefault(1.25f)
            .WithRange(0.75, 3.0)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.TabSize", EConfigValueType::Int)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Tab size in spaces")
            .WithDefault(4)
            .WithRange(1, 8)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.LineSpacing", EConfigValueType::Float)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Line spacing multiplier")
            .WithDefault(1.0f)
            .WithRange(1.0, 2.0)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.ShowWhitespace", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Render whitespace glyphs (spaces and tabs)")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.ShowLineNumbers", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Show line numbers in the gutter")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.ShowMiniMap", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Show the scrollbar mini-map")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.AutoIndent", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Auto-indent new lines based on surrounding scope")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.MatchBrackets", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Highlight matching brackets at the cursor")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.CompletePairs", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Auto-close paired glyphs (parentheses, brackets, quotes)")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.InsertSpacesOnTabs", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Insert spaces when the user presses Tab instead of a tab character")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.TrimTrailingOnSave", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Strip trailing whitespace from every line on save")
            .WithDefault(false)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.AutoReload", EConfigValueType::Bool)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Re-parse the buffer into the preview ~250ms after each edit")
            .WithDefault(true)
            .WithOwnerFile(EditorFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Editor.RmlUiEditor.Palette", EConfigValueType::String)
            .WithCategory("Editor/RmlUi Editor")
            .WithDescription("Color palette for the RmlUi editor (\"Dark\" or \"Light\")")
            .WithDefault(std::string("Dark"))
            .WithOwnerFile(EditorFile));
    }

    bool FEditorEngine::Init()
    {
        VFS::Mount<VFS::FNativeFileSystem>("/Editor", Paths::Combine(Paths::GetEngineDirectory(), "Editor"));

        GConfig->LoadPath("/Editor/Config");
        RegisterEditorSettings();

        bool bSuccess = FEngine::Init();

        entt::locator<entt::meta_ctx>::reset(Lumina::GetEngineMetaService());

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
