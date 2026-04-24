#include "pch.h"
#include "ConsoleVariable.h"
#include <nlohmann/json.hpp>
#include "Core/Assertions/Assert.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"


namespace Lumina
{
    FConsoleRegistry& FConsoleRegistry::Get()
    {
        static FConsoleRegistry Instance;
        return Instance;
    }

    void FConsoleRegistry::Register(FConsoleVariable&& Var)
    {
        FStringView VarName = Var.Name.data();
        ConsoleVariables.emplace(VarName, Move(Var));
    }

    FConsoleVariable* FConsoleRegistry::Find(FStringView Name)
    {
        auto It = ConsoleVariables.find(Name);
        return It != ConsoleVariables.end() ? &It->second : nullptr;
    }

    const FConsoleRegistry::FConsoleContainer& FConsoleRegistry::GetAll() const
    {
        return ConsoleVariables;
    }

    bool FConsoleRegistry::SetValueFromString(FStringView TargetName, FStringView StrValue)
    {
        FConsoleVariable* ConsoleVar = Find(TargetName);
        if (ConsoleVar == nullptr)
        {
            return false;
        }

        bool bSuccess = eastl::visit([&]<typename T0>(T0&&) -> bool
        {
            using T = eastl::decay_t<T0>;
            
            TOptional<T> ParsedValue = ConsoleHelpers::ParseValue<T>(StrValue);
            if (ParsedValue.has_value())
            {
                *(ConsoleVar->ValuePtr) = *ParsedValue;
                return true;
            }
            
            return false;
        }, *(ConsoleVar->ValuePtr));

        if (bSuccess && ConsoleVar->OnChange)
        {
            ConsoleVar->OnChange(*(ConsoleVar->ValuePtr));
            SaveToConfig();
        }

        return bSuccess;
    }
    
    TOptional<FString> FConsoleRegistry::GetValueAsString(FStringView VariableName)
    {
        FConsoleVariable* ConsoleVar = Find(VariableName);
        if (ConsoleVar == nullptr)
        {
            return eastl::nullopt;
        }

        FString Result;

        bool bSuccess = eastl::visit([&]<typename T0>(T0&& Value) -> bool
        {
            using T = eastl::decay_t<T0>;

            if constexpr (eastl::is_same_v<T, int>)
            {
                Result = FString(eastl::to_string(Value));
            }
            else if constexpr (eastl::is_same_v<T, float>)
            {
                Result = FString(eastl::to_string(Value));
            }
            else if constexpr (eastl::is_same_v<T, double>)
            {
                Result = FString(eastl::to_string(Value));
            }
            else if constexpr (eastl::is_same_v<T, bool>)
            {
                Result = Value ? "true" : "false";
            }
            else if constexpr (eastl::is_same_v<T, FString>)
            {
                Result = Value;
            }
            else
            {
                // Unsupported type
                return false;
            }

            return true;
        }, *(ConsoleVar->ValuePtr));

        if (!bSuccess)
        {
            return eastl::nullopt;
        }

        return Result;
    }

    void FConsoleRegistry::SaveToConfig()
    {
        // TODO: serialize console variables to Config/ConsoleSave.json.
    }

    void FConsoleRegistry::LoadFromConfig()
    {
        // TODO: restore console variables from Config/ConsoleSave.json.
    }
}
