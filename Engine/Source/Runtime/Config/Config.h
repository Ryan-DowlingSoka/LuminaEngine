#pragma once

#include "Core/Math/Math.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Templates/LuminaTemplate.h"
#include "nlohmann/json.hpp"

namespace Lumina
{
    /**
     * Type tag attached to a registered setting. Drives editor widget choice
     * and round-trip behavior. The on-disk JSON storage is the same regardless
     * of tag — strings are strings, numbers are numbers — but Path/Color/etc.
     * tell the project-settings editor how to render the row.
     */
    enum class EConfigValueType : uint8
    {
        Bool,
        Int,
        Float,
        String,
        Path,           // String value; editor shows a file picker
        DirectoryPath,  // String value; editor shows a directory picker
        Color,          // 4 floats [r,g,b,a]; editor shows ColorEdit4
        Vec2,           // 2 floats
        Vec3,           // 3 floats
        Vec4,           // 4 floats
        StringArray,
        Enum,           // String constrained to EnumOptions
    };

    /**
     * Declarative setting record. Modules call FConfig::RegisterSetting(...)
     * at init time to teach the project-settings editor about a key. Game
     * code can still Get/Set unregistered keys, but they won't show up in
     * the typed editor.
     */
    struct FConfigSetting
    {
        FString             Key;            // Dotted "Editor.WorldEditorTool.GuizmoSnapEnabled"
        EConfigValueType    Type            = EConfigValueType::String;
        nlohmann::json      DefaultValue;   // Falls back to this when key is missing on disk
        FString             Category;       // Slash-separated grouping for the editor: "Editor/World Tool"
        FString             Description;    // Tooltip in the editor
        FString             OwnerFile;      // VFS path of the file Set() should write to (e.g. "/Editor/Config/EditorSettings.json")
        FString             FileFilter;     // For Path: "Lua Script (*.luau)\0*.luau\0"
        TVector<FString>    EnumOptions;    // For Enum
        double              MinValue        = 0.0;
        double              MaxValue        = 0.0;
        bool                bHasRange       = false;

        // Builder helpers — keep registration call sites readable.
        FConfigSetting& WithDefault(nlohmann::json Value)       { DefaultValue = Move(Value); return *this; }
        FConfigSetting& WithCategory(FStringView InCategory)    { Category.assign(InCategory.data(), InCategory.size()); return *this; }
        FConfigSetting& WithDescription(FStringView InDesc)     { Description.assign(InDesc.data(), InDesc.size()); return *this; }
        FConfigSetting& WithOwnerFile(FStringView InFile)       { OwnerFile.assign(InFile.data(), InFile.size()); return *this; }
        FConfigSetting& WithFileFilter(FStringView InFilter)    { FileFilter.assign(InFilter.data(), InFilter.size()); return *this; }
        FConfigSetting& WithEnumOptions(TVector<FString> Opts)  { EnumOptions = Move(Opts); return *this; }
        FConfigSetting& WithRange(double Min, double Max)       { MinValue = Min; MaxValue = Max; bHasRange = true; return *this; }

        static FConfigSetting Make(FStringView InKey, EConfigValueType InType)
        {
            FConfigSetting S;
            S.Key.assign(InKey.data(), InKey.size());
            S.Type = InType;
            return S;
        }
    };

    RUNTIME_API extern class FConfig* GConfig;

    class RUNTIME_API FConfig
    {
    public:

        // Loads every .json file under the given VFS directory. Each file's
        // top-level keys are merged into the root config, and the file path
        // is recorded so subsequent Set() writes go back to the right file.
        void LoadPath(FStringView ConfigPath);

        // Schema declaration. Modules declare their settings here at startup.
        // Re-registering an existing key replaces the prior declaration.
        void RegisterSetting(FConfigSetting Setting);

        // Lookup a registered setting (nullptr if never declared).
        const FConfigSetting* FindSetting(FStringView Key) const;

        // Iterate every registered setting (in registration order).
        void ForEachSetting(const TFunction<void(const FConfigSetting&)>& Func) const;

        // Generic typed access. Falls back to Default if the key is missing.
        // For registered keys, the registered DefaultValue takes precedence
        // over the Default arg if the key is missing on disk.
        template<typename T>
        T Get(FStringView Key, const T& Default = T{}) const;

        // Generic typed write. Updates the in-memory tree and writes the
        // owning file to disk. Returns false if no owner file can be inferred.
        template<typename T>
        bool Set(FStringView Key, const T& Value);

        // Typed accessors. Each falls back to the registered default
        // (or T's default-constructed value) if the key is missing.
        bool                GetBool(FStringView Key) const;
        int32               GetInt(FStringView Key) const;
        float               GetFloat(FStringView Key) const;
        FString             GetString(FStringView Key) const;
        FString             GetPath(FStringView Key) const          { return GetString(Key); }
        FVector2            GetVec2(FStringView Key) const;
        FVector3            GetVec3(FStringView Key) const;
        FVector4            GetVec4(FStringView Key) const;
        FVector4            GetColor(FStringView Key) const         { return GetVec4(Key); }
        TVector<FString>    GetStringArray(FStringView Key) const;

        // Raw JSON access for the editor / advanced cases.
        const nlohmann::json* GetRaw(FStringView Key) const;

    private:

        bool SetRaw(FStringView Key, nlohmann::json Value);

        nlohmann::json* NavigateToNode(FStringView Key, bool bCreate);
        const nlohmann::json* NavigateToNode(FStringView Key) const;

        // Resolves which file owns Key: explicit registration first,
        // then existing PathToFile entry, else "".
        FString FindOwnerFile(FStringView Key) const;

        // Walk a JSON subtree and record every leaf path under the file it lives in.
        void IndexPathsForFile(const nlohmann::json& Obj, const FString& Prefix, const FString& File);

        nlohmann::json                              RootConfig;
        THashMap<FString, nlohmann::json>           FileConfigs;        // VFS path -> parsed json
        THashMap<FString, FString>                  PathToFile;         // dotted-key prefix -> VFS path

        TVector<FString>                            RegistryOrder;      // for stable iteration
        THashMap<FString, FConfigSetting>           Registry;
    };

    // Template definitions ------------------------------------------------

    template<typename T>
    T FConfig::Get(FStringView Key, const T& Default) const
    {
        const nlohmann::json* Node = NavigateToNode(Key);
        if (Node != nullptr)
        {
            try
            {
                return Node->get<T>();
            }
            catch (...)
            {
                // Fall through to default — value type didn't match.
            }
        }

        if (const FConfigSetting* Setting = FindSetting(Key))
        {
            if (!Setting->DefaultValue.is_null())
            {
                try { return Setting->DefaultValue.get<T>(); }
                catch (...) {}
            }
        }

        return Default;
    }

    template<typename T>
    bool FConfig::Set(FStringView Key, const T& Value)
    {
        return SetRaw(Key, nlohmann::json(Value));
    }
}
