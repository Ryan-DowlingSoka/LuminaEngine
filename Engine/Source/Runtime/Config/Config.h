#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "nlohmann/json.hpp"

namespace Lumina
{
    RUNTIME_API extern class FConfig* GConfig;
    class RUNTIME_API FConfig
    {
    public:

        void LoadPath(FStringView ConfigPath);

        template<typename T>
        T Get(FStringView Key, const T& Defaults = T{});
        
        const nlohmann::json* GetJson(FStringView Key);

        bool Set(const FString& Path, const nlohmann::json& Value);

        template<typename TFunc>
        void ForEach(TFunc&& Func);

    private:
        
        FString FindSourceFile(const FString& Path) const;
        void MergeJson(nlohmann::json& Target, const nlohmann::json& Source);
        nlohmann::json* NavigateToNode(FStringView Path);
        const nlohmann::json* NavigateToNode(FStringView Path) const;

    private:

        nlohmann::json RootConfig;

        THashMap<FString, FString> PathToFile;
        
        THashMap<FString, nlohmann::json> FileConfigs;
    };


    template <typename T>
    T FConfig::Get(FStringView Key, const T& Defaults)
    {
        std::string KeyStr = Key.data();

        nlohmann::json* Current = &RootConfig;
        size_t Pos = 0;

        while ((Pos = KeyStr.find('.')) != std::string::npos)
        {
            std::string Part = KeyStr.substr(0, Pos);

            if (!Current->contains(Part) || !(*Current)[Part].is_object())
            {
                return Defaults;
            }

            Current = &(*Current)[Part];
            KeyStr.erase(0, Pos + 1);
        }

        if (Current->contains(KeyStr))
        {
            return (*Current)[KeyStr].get<T>();
        }

        return Defaults;
    }

    template <typename TFunc>
    void FConfig::ForEach(TFunc&& Func)
    {
        for (auto It = RootConfig.begin(); It != RootConfig.end(); ++It)
        {
            FString Key = It.key().c_str();
            eastl::invoke(Func, Key, *It);
        }
    }
}
