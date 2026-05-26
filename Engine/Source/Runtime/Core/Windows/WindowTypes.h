#pragma once
#include <Containers/String.h>
#include <glm/fwd.hpp>

namespace Lumina
{
    struct FWindowSpecs
    {
        FString Title = "Lumina";
        glm::uvec2 Extent{};
        bool bFullscreen = false;
        bool bShowTitlebar = false;
    };

    enum class ECursorMode
    {
        Normal,     // visible, free
        Hidden,     // hidden, free
        Disabled,   // hidden, locked/captured
    };
}
