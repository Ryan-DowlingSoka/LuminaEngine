#include "pch.h"
#include "Config.h"

#include "Core/Math/Math.h"

#include "FileSystem/FileSystem.h"
#include "Log/Log.h"

using Json = nlohmann::json;

namespace Lumina
{
    RUNTIME_API FConfig* GConfig;

    namespace
    {
        // Split a dotted key like "Editor.WorldEditorTool.GuizmoSnapEnabled"
        // into its segments. Returns false (and leaves Out empty) for empty Key.
        bool SplitDottedKey(FStringView Key, TVector<FString>& Out)
        {
            Out.clear();
            if (Key.empty())
            {
                return false;
            }

            size_t Start = 0;
            for (size_t i = 0; i < Key.size(); ++i)
            {
                if (Key[i] == '.')
                {
                    Out.emplace_back(Key.data() + Start, Key.data() + i);
                    Start = i + 1;
                }
            }
            Out.emplace_back(Key.data() + Start, Key.data() + Key.size());
            return !Out.empty();
        }

        // Read the leading "GroupKey" out of a dotted path. Used so an unregistered
        // key under "Editor.X.Y" resolves to the same file as registered "Editor.A.B".
        FString TopLevelOf(FStringView Key)
        {
            size_t Dot = Key.find('.');
            if (Dot == FStringView::npos)
            {
                return FString(Key.data(), Key.size());
            }
            return FString(Key.data(), Key.data() + Dot);
        }

        Json VecToJson(const float* Data, int32 N)
        {
            Json Array = Json::array();
            for (int32 i = 0; i < N; ++i)
            {
                Array.push_back(Data[i]);
            }
            return Array;
        }

        // Reads up to N floats from a JSON array, padding with 0 for short/wrong-typed input.
        bool JsonToVec(const Json& Node, float* Out, int32 N)
        {
            if (!Node.is_array())
            {
                return false;
            }
            for (int32 i = 0; i < N; ++i)
            {
                Out[i] = (i < (int32)Node.size() && Node[i].is_number())
                    ? Node[i].get<float>()
                    : 0.0f;
            }
            return true;
        }
    }

    void FConfig::LoadPath(FStringView ConfigPath)
    {
        VFS::DirectoryIterator(ConfigPath, [&](const VFS::FFileInfo& Info)
        {
            if (Info.GetExt() != ".json")
            {
                return;
            }

            FString Result;
            if (!VFS::ReadFile(Result, Info.VirtualPath))
            {
                return;
            }

            Json Parsed;
            try
            {
                Parsed = Json::parse(Result.c_str());
            }
            catch (const std::exception& Ex)
            {
                LOG_ERROR("FConfig: failed to parse {0} ({1})", Info.VirtualPath.c_str(), Ex.what());
                return;
            }

            FString FilePath(Info.VirtualPath.c_str(), Info.VirtualPath.size());
            FileConfigs[FilePath] = Parsed;

            IndexPathsForFile(Parsed, FString(), FilePath);

            // Merge into the root tree. Later files take precedence — this is
            // intentional for project-overrides-engine layering, the same as before.
            for (auto It = Parsed.begin(); It != Parsed.end(); ++It)
            {
                if (It->is_object() && RootConfig.contains(It.key()) && RootConfig[It.key()].is_object())
                {
                    RootConfig[It.key()].merge_patch(*It);
                }
                else
                {
                    RootConfig[It.key()] = *It;
                }
            }
        });
    }

    void FConfig::IndexPathsForFile(const Json& Obj, const FString& Prefix, const FString& File)
    {
        if (!Obj.is_object())
        {
            return;
        }

        for (auto It = Obj.begin(); It != Obj.end(); ++It)
        {
            FString Key = It.key().c_str();
            FString FullPath = Prefix.empty() ? Key : (Prefix + "." + Key);

            PathToFile[FullPath] = File;

            if (It->is_object())
            {
                IndexPathsForFile(*It, FullPath, File);
            }
        }
    }

    void FConfig::RegisterSetting(FConfigSetting Setting)
    {
        FString Key = Setting.Key;
        if (Registry.find(Key) == Registry.end())
        {
            RegistryOrder.push_back(Key);
        }
        Registry[Key] = Move(Setting);
    }

    const FConfigSetting* FConfig::FindSetting(FStringView Key) const
    {
        FString KeyStr(Key.data(), Key.size());
        auto It = Registry.find(KeyStr);
        return It == Registry.end() ? nullptr : &It->second;
    }

    void FConfig::ForEachSetting(const TFunction<void(const FConfigSetting&)>& Func) const
    {
        for (const FString& Key : RegistryOrder)
        {
            auto It = Registry.find(Key);
            if (It != Registry.end())
            {
                Func(It->second);
            }
        }
    }

    const Json* FConfig::GetRaw(FStringView Key) const
    {
        return NavigateToNode(Key);
    }

    bool FConfig::SetRaw(FStringView Key, Json Value)
    {
        TVector<FString> Parts;
        if (!SplitDottedKey(Key, Parts))
        {
            return false;
        }

        FString OwnerFile = FindOwnerFile(Key);
        if (OwnerFile.empty())
        {
            LOG_WARN("FConfig::Set: no owner file for '{0}' (register the setting first or load a config file containing it)", FString(Key.data(), Key.size()).c_str());
            return false;
        }

        // Update the unified tree.
        Json* Current = &RootConfig;
        for (size_t i = 0; i + 1 < Parts.size(); ++i)
        {
            const std::string PartStr = Parts[i].c_str();
            if (!Current->contains(PartStr) || !(*Current)[PartStr].is_object())
            {
                (*Current)[PartStr] = Json::object();
            }
            Current = &(*Current)[PartStr];
        }
        (*Current)[Parts.back().c_str()] = Value;

        // Mirror into the owner file's tree, then flush.
        Json& File = FileConfigs[OwnerFile];
        Current = &File;
        for (size_t i = 0; i + 1 < Parts.size(); ++i)
        {
            const std::string PartStr = Parts[i].c_str();
            if (!Current->contains(PartStr) || !(*Current)[PartStr].is_object())
            {
                (*Current)[PartStr] = Json::object();
            }
            Current = &(*Current)[PartStr];
        }
        (*Current)[Parts.back().c_str()] = Move(Value);

        // Track the new key so future Get->Set round-trips know its owner.
        PathToFile[FString(Key.data(), Key.size())] = OwnerFile;

        const std::string Dumped = File.dump(4);
        return VFS::WriteFile(OwnerFile, FStringView(Dumped.c_str(), Dumped.size()));
    }

    FString FConfig::FindOwnerFile(FStringView Key) const
    {
        // 1) Explicit registration wins.
        if (const FConfigSetting* Setting = FindSetting(Key))
        {
            if (!Setting->OwnerFile.empty())
            {
                return Setting->OwnerFile;
            }
        }

        // 2) Did this exact key (or any ancestor) come from a known file?
        FString KeyStr(Key.data(), Key.size());
        auto It = PathToFile.find(KeyStr);
        if (It != PathToFile.end())
        {
            return It->second;
        }

        FString Cursor = KeyStr;
        size_t LastDot = Cursor.find_last_of('.');
        while (LastDot != FString::npos)
        {
            Cursor = Cursor.substr(0, LastDot);
            It = PathToFile.find(Cursor);
            if (It != PathToFile.end())
            {
                return It->second;
            }
            LastDot = Cursor.find_last_of('.');
        }

        // 3) Last resort: any registered setting under the same top-level group.
        const FString Group = TopLevelOf(Key);
        for (const auto& [RegKey, RegSetting] : Registry)
        {
            if (!RegSetting.OwnerFile.empty() && TopLevelOf(RegKey) == Group)
            {
                return RegSetting.OwnerFile;
            }
        }

        return FString();
    }

    Json* FConfig::NavigateToNode(FStringView Key, bool bCreate)
    {
        TVector<FString> Parts;
        if (!SplitDottedKey(Key, Parts))
        {
            return nullptr;
        }

        Json* Current = &RootConfig;
        for (size_t i = 0; i < Parts.size(); ++i)
        {
            const std::string PartStr = Parts[i].c_str();
            const bool bLast = (i + 1 == Parts.size());

            if (Current->contains(PartStr))
            {
                Current = &(*Current)[PartStr];
                continue;
            }

            if (!bCreate)
            {
                return nullptr;
            }

            (*Current)[PartStr] = bLast ? Json() : Json::object();
            Current = &(*Current)[PartStr];
        }
        return Current;
    }

    const Json* FConfig::NavigateToNode(FStringView Key) const
    {
        return const_cast<FConfig*>(this)->NavigateToNode(Key, false);
    }

    // Typed accessors -----------------------------------------------------

    namespace
    {
        // Common path: read the json node, fall back to the registered default,
        // then to the caller-default. Centralizes the try/catch dance.
        template<typename T, typename TDefault>
        T GetTypedFallback(const FConfig& Cfg, FStringView Key, TDefault&& Fallback)
        {
            if (const Json* Node = Cfg.GetRaw(Key))
            {
                try { return Node->get<T>(); } catch (...) {}
            }
            if (const FConfigSetting* Setting = Cfg.FindSetting(Key))
            {
                if (!Setting->DefaultValue.is_null())
                {
                    try { return Setting->DefaultValue.get<T>(); } catch (...) {}
                }
            }
            return T(std::forward<TDefault>(Fallback));
        }
    }

    bool FConfig::GetBool(FStringView Key) const
    {
        return GetTypedFallback<bool>(*this, Key, false);
    }

    int32 FConfig::GetInt(FStringView Key) const
    {
        return GetTypedFallback<int32>(*this, Key, 0);
    }

    float FConfig::GetFloat(FStringView Key) const
    {
        return GetTypedFallback<float>(*this, Key, 0.0f);
    }

    FString FConfig::GetString(FStringView Key) const
    {
        if (const Json* Node = GetRaw(Key))
        {
            if (Node->is_string())
            {
                return FString(Node->get<std::string>().c_str());
            }
        }
        if (const FConfigSetting* Setting = FindSetting(Key))
        {
            if (Setting->DefaultValue.is_string())
            {
                return FString(Setting->DefaultValue.get<std::string>().c_str());
            }
        }
        return FString();
    }

    FVector2 FConfig::GetVec2(FStringView Key) const
    {
        FVector2 Out(0.0f);
        if (const Json* Node = GetRaw(Key))
        {
            JsonToVec(*Node, &Out.x, 2);
        }
        else if (const FConfigSetting* Setting = FindSetting(Key))
        {
            JsonToVec(Setting->DefaultValue, &Out.x, 2);
        }
        return Out;
    }

    FVector3 FConfig::GetVec3(FStringView Key) const
    {
        FVector3 Out(0.0f);
        if (const Json* Node = GetRaw(Key))
        {
            JsonToVec(*Node, &Out.x, 3);
        }
        else if (const FConfigSetting* Setting = FindSetting(Key))
        {
            JsonToVec(Setting->DefaultValue, &Out.x, 3);
        }
        return Out;
    }

    FVector4 FConfig::GetVec4(FStringView Key) const
    {
        FVector4 Out(0.0f);
        if (const Json* Node = GetRaw(Key))
        {
            JsonToVec(*Node, &Out.x, 4);
        }
        else if (const FConfigSetting* Setting = FindSetting(Key))
        {
            JsonToVec(Setting->DefaultValue, &Out.x, 4);
        }
        return Out;
    }

    TVector<FString> FConfig::GetStringArray(FStringView Key) const
    {
        TVector<FString> Out;
        const Json* Node = GetRaw(Key);
        if (Node == nullptr || !Node->is_array())
        {
            if (const FConfigSetting* Setting = FindSetting(Key))
            {
                if (Setting->DefaultValue.is_array())
                {
                    Node = &Setting->DefaultValue;
                }
            }
        }

        if (Node != nullptr && Node->is_array())
        {
            Out.reserve(Node->size());
            for (const Json& Element : *Node)
            {
                if (Element.is_string())
                {
                    Out.emplace_back(Element.get<std::string>().c_str());
                }
            }
        }
        return Out;
    }
}
