-- Example: listening for AnimNotify events authored in the Animation Editor.
--
-- Author notifies on a clip in the Animation Editor (Notifies tab): drop a
-- "Footstep" point notify on a track, and a "WeaponTrail" notify state (a
-- ranged bar). Then attach this script to an entity that has both an
-- SSkeletalMeshComponent and an SSimpleAnimationComponent playing that clip.
--
-- Bindings are per-entity: you grab the component (which references the clip
-- it plays) and bind handlers by notify name. The animation system fires them
-- as the playhead crosses the notify during playback.

local EntityScript = require("Stdlib/EntityScript")
local Script: EntityScript = EntityScript.new()

function Script:OnReady()
    local anim = self:GetComponent(SSimpleAnimationComponent)
    if not anim then
        return
    end

    -- Point notify: fires once each time the playhead crosses it.
    --   (entity, name, time)
    anim:BindNotify("Footstep", function(entity, name, time)
        print(string.format("[Notify] %s @ %.3fs", name, time))
        -- e.g. play a footstep sound or spawn a dust decal here.
    end)

    -- Notify state: ranged. OnBegin/OnTick(alpha 0..1)/OnEnd. Any may be nil.
    anim:BindNotifyState("WeaponTrail",
        function(entity, name, startTime)            -- OnBegin
            self.TrailActive = true
        end,
        function(entity, name, alpha)                -- OnTick
            -- alpha sweeps 0 -> 1 across the range; drive a trail width, etc.
        end,
        function(entity, name, endTime)              -- OnEnd
            self.TrailActive = false
        end)
end

-- Defining OnEditorUpdate would let this run in the editor too; left out so it
-- only runs in play mode.

return Script
