#pragma once

namespace Lumina
{
    // Drives FEventProcessor routing. Mirrors Unreal's FInputMode.
    enum class EInputMode : uint8
    {
        Game,
        UI,
        GameAndUI,
    };

    // Set by handlers; matched against EInputMode to decide who gets events.
    // Editor handlers always dispatch regardless of mode.
    enum class EInputCategory : uint8
    {
        Game,
        UI,
        Editor,
    };

    inline const char* InputModeToString(EInputMode Mode)
    {
        switch (Mode)
        {
        case EInputMode::Game:      return "Game";
        case EInputMode::UI:        return "UI";
        case EInputMode::GameAndUI: return "GameAndUI";
        }
        return "Game";
    }
}
