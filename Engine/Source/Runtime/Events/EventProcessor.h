#pragma once

#include "Containers/Array.h"
#include "Events/Event.h"
#include "Input/InputMode.h"

namespace Lumina
{
    class IEventHandler
    {
    public:

        /** Called when an event is received, return true to stop propagation and mark as handled. */
        virtual bool OnEvent(FEvent& Event) { return false; }

        /** Routing tag used by FEventProcessor to gate dispatch by input mode.
            Editor handlers ignore the mode and always dispatch. */
        virtual EInputCategory GetInputCategory() const { return EInputCategory::Game; }
    };

    class FEventProcessor
    {
    public:

        void RegisterEventHandler(IEventHandler* InHandler);
        void UnregisterEventHandler(IEventHandler* InHandler);

        void Clear();

        RUNTIME_API void SetInputMode(EInputMode Mode);
        RUNTIME_API EInputMode GetInputMode() const { return InputMode; }


        template<typename TEvent, typename... Args>
        requires(eastl::is_base_of_v<FEvent, TEvent> && eastl::is_constructible_v<TEvent, Args&&...>)
        void Dispatch(Args&&... InArgs)
        {
            TEvent Event(eastl::forward<Args>(InArgs)...);
            DispatchEvent(Event);
        }


    private:

        void DispatchEvent(FEvent& Event);
        bool ShouldRouteTo(IEventHandler* Handler) const;

        TVector<IEventHandler*> EventHandlers;
        EInputMode              InputMode = EInputMode::Game;
    };
}
