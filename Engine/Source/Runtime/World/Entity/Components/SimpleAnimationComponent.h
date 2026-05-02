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

        /**
         * Set by the animation system the frame after a non-looping animation
         * completes. Stays true until the next PlayAnimation() call. Lets game
         * code react to "the punch finished" without manually polling time.
         */
        PROPERTY(Script, Category = "Animation")
        bool bFinished = false;

        // Internal: tells the system the pose needs a fresh sample even when
        // bPlaying is false (e.g. the user just called PlayAnimation but
        // hasn't ticked yet, or rewound CurrentTime). The system clears it
        // after sampling.
        bool bDirty = true;

        /**
         * Fire-and-forget play. Resets time to 0, marks the clip dirty, and
         * starts advancing. Pass bLoop=false for one-shot animations -- the
         * system stops playback and sets bFinished=true on completion.
         *
         * Calling this with the same clip restarts it from the beginning;
         * passing nullptr stops playback and clears the active clip.
         */
        FUNCTION(Script)
        void PlayAnimation(CAnimation* InAnimation, bool bLoop = false, float Speed = 1.0f)
        {
            Animation       = InAnimation;
            CurrentTime     = 0.0f;
            PlaybackSpeed   = Speed;
            bLooping        = bLoop;
            bPlaying        = (InAnimation != nullptr);
            bFinished       = false;
            bDirty          = true;
        }

        /** Stop playback and freeze the pose at the current time. */
        FUNCTION(Script)
        void Pause()
        {
            bPlaying = false;
        }

        /** Resume playback from the current time without resetting it. */
        FUNCTION(Script)
        void Resume()
        {
            if (Animation.IsValid())
            {
                bPlaying  = true;
                bFinished = false;
            }
        }

        /**
         * Stop playback and snap back to time 0. Leaves the pose in its bind
         * pose on the next sample so the mesh visibly returns to rest.
         */
        FUNCTION(Script)
        void Stop()
        {
            bPlaying    = false;
            CurrentTime = 0.0f;
            bFinished   = false;
            bDirty      = true;
        }

        /** True while a non-looping clip has run to completion. */
        FUNCTION(Script)
        bool IsFinished() const { return bFinished; }

        /** True while the component is actively advancing the animation. */
        FUNCTION(Script)
        bool IsPlaying() const { return bPlaying; }
    };
}
