#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Core/Object/SoftObjectPtr.h"
#include "Config/DeveloperSettings.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "World/World.h"
#include "Assets/AssetRef.h"
#include "EngineSettings.generated.h"

namespace Lumina
{
    // Per-project runtime settings; persists to the project's /Config/GameSettings.json.
    REFLECT(MinimalAPI, ConfigFile = "/Config/GameSettings.json", DisplayName = "Project", Category = "Project")
    class CProjectSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Lua module loaded after the project DLL is loaded. Rename-safe (GUID-backed). */
        PROPERTY(Editable, Category = "Scripting", AssetType = "luau")
        FAssetRef LuaModuleFile;

        /** Reflected CGameInstance subclass to instantiate at runtime. Empty = base CGameInstance. */
        PROPERTY(Editable, Category = "Scripting")
        FString GameInstanceClass;

        /** World loaded when the standalone game starts. */
        PROPERTY(Editable, Category = "Maps")
        TSoftObjectPtr<CWorld> GameStartupMap;

        /** World opened automatically when the editor finishes loading the project. */
        PROPERTY(Editable, Category = "Maps")
        TSoftObjectPtr<CWorld> EditorStartupMap;

        /** Worlds the cooker walks from to build the shipped PAK. */
        PROPERTY(Editable, Category = "Maps")
        TVector<TSoftObjectPtr<CWorld>> CookRoots;
    };

    // Editor-wide preferences + launch state. Lives in the runtime module so the runtime
    // ImGui renderer can read UIScale, while the editor edits it through the Settings panel.
    REFLECT(MinimalAPI, ConfigFile = "/Editor/Config/EditorPreferences.json", DisplayName = "General", Category = "Editor")
    class CEditorSettings : public CDeveloperSettings
    {
        GENERATED_BODY()
    public:

        /** Editor UI scale. 0 = auto (monitor DPI + resolution); otherwise an explicit factor (1.0 = 100%). */
        PROPERTY(Editable, Category = "Appearance", ClampMin = 0.0f, ClampMax = 3.0f)
        float UIScale = 0.0f;
    };
}
