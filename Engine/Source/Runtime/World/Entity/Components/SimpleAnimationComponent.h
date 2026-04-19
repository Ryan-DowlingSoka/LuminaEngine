#pragma once
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "SimpleAnimationComponent.generated.h"

namespace Lumina
{
    class CAnimation;
    
    REFLECT(Component)
    struct SSimpleAnimationComponent
    {
        GENERATED_BODY()
        
        /** The animation asset to play on the skeletal mesh. */
        PROPERTY(Script, Editable, Category = "Animation")
        TObjectPtr<CAnimation> Animation;

        /** Current playback position within the animation (seconds). */
        PROPERTY(Script, Editable, Category = "Animation")
        float CurrentTime = 0.0f;

        /** Playback rate multiplier (1.0 = normal speed, 2.0 = double speed). */
        PROPERTY(Script, Editable, Category = "Animation")
        float PlaybackSpeed = 1.0f;

        /** When true, the animation restarts automatically upon completion. */
        PROPERTY(Script, Editable, Category = "Animation")
        bool bLooping = true;

        /** When true, the animation is currently advancing. */
        PROPERTY(Script, Editable, Category = "Animation")
        bool bPlaying = true;
    };
}
