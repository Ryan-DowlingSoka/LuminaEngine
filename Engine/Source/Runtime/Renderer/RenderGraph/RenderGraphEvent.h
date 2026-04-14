#pragma once

#include "Containers/String.h"

namespace Lumina
{
    class RUNTIME_API FRGEvent
    {
    public:
        FRGEvent() = default;

        explicit FRGEvent(const char* InEventName)
            : StaticName(InEventName)
        {}
        
        explicit FRGEvent(FStringView InEventName)
            : StaticName(InEventName.data())
        {}
        
        const char* Get() const
        {
            return StaticName;
        }

        bool IsValid() const
        {
            return StaticName != nullptr;
        }

    private:
        const char* StaticName = nullptr;
    };
}
