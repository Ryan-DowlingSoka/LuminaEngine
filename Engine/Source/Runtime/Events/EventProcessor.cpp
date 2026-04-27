#include "pch.h"
#include "EventProcessor.h"

#include "Log/Log.h"

namespace Lumina
{
    void FEventProcessor::RegisterEventHandler(IEventHandler* InHandler)
    {
        if (eastl::find(EventHandlers.begin(), EventHandlers.end(), InHandler) != EventHandlers.end())
        {
            LOG_ERROR("Event Handler Already Registered!");
            return;
        }

        EventHandlers.push_back(InHandler);
    }

    void FEventProcessor::UnregisterEventHandler(IEventHandler* InHandler)
    {
        auto It = eastl::find(EventHandlers.begin(), EventHandlers.end(), InHandler);
        if (It != EventHandlers.end())
        {
            EventHandlers.erase(It);
        }
    }

    void FEventProcessor::Clear()
    {
        EventHandlers.clear();
    }

    void FEventProcessor::SetInputMode(EInputMode Mode)
    {
        InputMode = Mode;
    }

    bool FEventProcessor::ShouldRouteTo(IEventHandler* Handler) const
    {
        const EInputCategory Category = Handler->GetInputCategory();
        if (Category == EInputCategory::Editor)
        {
            return true;
        }

        switch (InputMode)
        {
        case EInputMode::Game:      return Category == EInputCategory::Game;
        case EInputMode::UI:        return Category == EInputCategory::UI;
        case EInputMode::GameAndUI: return true;
        }
        return false;
    }

    void FEventProcessor::DispatchEvent(FEvent& Event)
    {
        // GameAndUI: UI gets first crack so it can intercept before game.
        if (InputMode == EInputMode::GameAndUI)
        {
            for (IEventHandler* Handler : EventHandlers)
            {
                if (Handler->GetInputCategory() != EInputCategory::UI) continue;
                if (Handler->OnEvent(Event)) return;
            }
            for (IEventHandler* Handler : EventHandlers)
            {
                if (Handler->GetInputCategory() == EInputCategory::UI) continue;
                if (Handler->OnEvent(Event)) return;
            }
            return;
        }

        for (IEventHandler* Handler : EventHandlers)
        {
            if (!ShouldRouteTo(Handler)) continue;
            if (Handler->OnEvent(Event)) return;
        }
    }
}
