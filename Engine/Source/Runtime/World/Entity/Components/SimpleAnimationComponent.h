#pragma once
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Scripting/Lua/Reference.h"
#include "SimpleAnimationComponent.generated.h"

namespace Lumina
{
    class CAnimation;

    /**
     * Lua callbacks for one bound NotifyState (ranged notify). Any of the three
     * refs may be empty; the animation system only invokes the ones that are set.
     */
    struct FNotifyStateBinding
    {
        Lua::FRef OnBegin;
        Lua::FRef OnTick;
        Lua::FRef OnEnd;
    };

    REFLECT(Component, Category = "Animation")
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

        // Forces a fresh pose sample even when not playing (e.g. just-called PlayAnimation or rewound
        // CurrentTime). Cleared by the system after sampling.
        bool bDirty = true;

        // AnimNotify runtime state (transient): mirrors the cached-FRef pattern on SScriptComponent --
        // gameplay binds Lua callbacks by notify name; SSimpleAnimationSystem fires them on playhead crossings.

        // CurrentTime before this frame's advance, used to detect playhead crossings.
        float PreviousTime = 0.0f;

        // Set when CurrentTime advanced via playback (not a scrub/stop jump). Point notifies + NotifyState
        // Tick fire only when true, so Stop()/seek don't re-fire events. End still fires on stop.
        bool bAdvancedThisFrame = false;

        // Point-notify handlers, keyed by notify name. A name may carry many handlers.
        THashMap<FName, TVector<Lua::FRef>> NotifyHandlers;

        // Ranged NotifyState handlers (begin/tick/end), keyed by notify name.
        THashMap<FName, TVector<FNotifyStateBinding>> NotifyStateHandlers;

        // Indices (into the active clip's NotifyStates) currently under the playhead.
        // Diffed each frame to drive Begin/End transitions; cleared on clip change.
        TVector<int32> ActiveNotifyStates;

        bool HasNotifyBindings() const { return !NotifyHandlers.empty() || !NotifyStateHandlers.empty(); }

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
            PreviousTime    = 0.0f;
            PlaybackSpeed   = Speed;
            bLooping        = bLoop;
            bPlaying        = (InAnimation != nullptr);
            bFinished       = false;
            bDirty          = true;
            ActiveNotifyStates.clear();
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

        /**
         * Bind a Lua callback to a point notify by name. The handler fires once each
         * time the playhead crosses a notify with this name on the active clip:
         *
         *   anim:BindNotify("Footstep", function(entity, name, time) ... end)
         *
         * Multiple callbacks may be bound to the same name. Bindings live on this
         * component instance, so they are specific to this entity's playback.
         */
        FUNCTION(Script)
        void BindNotify(FName NotifyName, Lua::FRef Callback)
        {
            if (Callback.IsInvokable())
            {
                NotifyHandlers[NotifyName].push_back(eastl::move(Callback));
            }
        }

        /**
         * Bind Lua callbacks to a ranged NotifyState by name. OnBegin fires when the
         * playhead enters the range, OnTick every frame while inside (receiving the
         * 0..1 alpha through the range), and OnEnd when it leaves (or the clip stops).
         * Any callback may be nil:
         *
         *   anim:BindNotifyState("WeaponTrail", onBegin, onTick, onEnd)
         */
        FUNCTION(Script)
        void BindNotifyState(FName NotifyName, Lua::FRef OnBegin, Lua::FRef OnTick, Lua::FRef OnEnd)
        {
            FNotifyStateBinding Binding;
            Binding.OnBegin = eastl::move(OnBegin);
            Binding.OnTick  = eastl::move(OnTick);
            Binding.OnEnd   = eastl::move(OnEnd);
            NotifyStateHandlers[NotifyName].push_back(eastl::move(Binding));
        }

        /** Remove every notify and notify-state binding on this component. */
        FUNCTION(Script)
        void ClearNotifyBindings()
        {
            NotifyHandlers.clear();
            NotifyStateHandlers.clear();
            ActiveNotifyStates.clear();
        }
    };
}
