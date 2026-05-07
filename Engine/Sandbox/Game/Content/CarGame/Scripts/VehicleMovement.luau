local VehicleMovement: EntityScript = EntityScript.new()

-- Tunables -----------------------------------------------------------------
-- Stiffness/damping are forces (N), applied via ApplyForceAt so Jolt
-- integrates them across the fixed step. The previous formulation called
-- ApplyImpulseAt with these same values, which treated N as N·s and
-- effectively multiplied them by PhysicsHz — stable at rest but unstable at
-- speed when compression varied. New numbers ≈ old × 60Hz to preserve feel.
local SuspensionStiffness = 120000.0 -- spring force per metre of compression
local SuspensionDamping   = 8000.0  -- damper force per m/s along spring
local MaxSuspensionForce  = 200000.0 -- cap so a noisy ray hit can't punt the body
local RestLength          = 0.45
local RayExtra            = 0.00       -- extra ray length past rest, for slopes

local EngineAccel         = 22.0      -- m/s² at full throttle
local ReverseFactor       = 0.45      -- reverse is weaker than forward
local BrakeAccel          = 36.0      -- m/s² when throttle opposes motion
local CoastDrag           = 0.9       -- 1/s exponential decay when no input
local MaxSpeed            = 55.0      -- m/s

local SteerYawRateMax     = 2.6       -- max yaw rate (rad/s)
local SteerSpeedFalloff   = 30.0      -- speed at which steering authority caps
local SteerResponse       = 7.0       -- yaw blend rate toward target

local ThrottleAttack      = 10.0      -- pedal-down response (1/s)
local ThrottleRelease     = 3.0       -- pedal-up response (1/s) — slower for realism

local LateralGrip         = 3.0       -- side-velocity decay (1/s)
local DriftLateralGrip    = 0.5       -- with handbrake — lets the rear slide

-- Wheel layout in body frame ----------------------------------------------
local HalfLength = 1.0
local HalfWidth  = 1.0
local WheelDrop  = -0.1

local DebugWheels = false   -- draw the wheel rays

local bHandbreakLastFrame = false

-- V8 engine synth ---------------------------------------------------------
-- Additive synthesis at the cylinder-firing frequency. A V8 fires 4 times per
-- crank revolution (8 cylinders × 4-stroke / 2), so firing_Hz = RPM/15. We
-- stack a handful of harmonics with throttle-dependent amplitudes to brighten
-- the tone under load, modulate one harmonic with a sub-firing tremolo to fake
-- the cross-plane "burble", and bandlimit-noise on top for exhaust rasp.
local AudioSampleRate    = 48000
local AudioBufferFrames  = 16384
local AudioTargetQueued  = 4096    -- frames we try to keep queued ahead
local TWO_PI             = 2.0 * math.pi

local IdleRPM            = 850
local RedlineRPM         = 7200

local HUDDocument        = "/Game/CarGame/UI/UI_HUD.rml"
local HUDUpdateRate      = 20.0           -- Hz; HUD doesn't need per-frame churn
local MetersPerSecToMPH  = 2.2369363

-- Boost meter
local BoostDrainRate     = 0.85     -- meter per second while held
local BoostRechargeRate  = 0.18     -- meter per second when idle
local BoostMin           = 0.05     -- below this, must recharge before reuse
local BoostMultiplier    = 3.0      -- accel multiplier while burning boost

-- Drift scoring
local DriftMinSpeed      = 5.0      -- m/s; below this drift doesn't count
local DriftMinSlip       = 2.0      -- m/s of lateral velocity
local DriftScoreRate     = 8.0      -- score per (slip * speed * mult) per second
local DriftMultRampUp    = 0.15     -- multiplier added per second of drift
local DriftMultMax       = 4.0
local DriftBankFade      = 1.5      -- seconds banner stays after drift ends
local DriftBoostReward   = 0.0005   -- boost meter awarded per banked score point
local DriftHugeThreshold = 5000

-- Drift sprint mode --------------------------------------------------------
-- Fixed-duration countdown; the player tries to rack up as many drift points
-- as possible before the clock hits 0. R reloads the level for a fresh run.
local LevelPath              = "/Game/CarGame/L_RaceTrack.lasset"
local RunDurationSeconds     = 90.0
local TimerWarnAt            = 15.0      -- timer border turns hot below this
local TimerCriticalAt        = 5.0       -- timer pulses below this

-- Crash penalty: lose score on collision. Cost scales with impact speed so
-- a gentle nudge is cheap and a head-on hit is brutal.
local CrashMinImpact      = 4.0      -- m/s; below this we ignore (kerb-touches etc.)
local CrashHeavyImpact    = 14.0     -- m/s; "heavy" UI styling kicks in above this
local CrashScorePerImpact = 1000.0    -- penalty = ImpactSpeed * this
local CrashScoreMin       = 50       -- minimum penalty when above CrashMinImpact
local CrashScoreMax       = 20000     -- ceiling so a single hit can't wipe everything
local CrashBannerHold     = 1.4      -- seconds the banner stays up after a crash
local CrashCooldown       = 0.35     -- seconds before another crash can register

-- Redline visual cutoff
local RedlineWarn        = 0.93

-- Per-harmonic amplitudes at idle and at full throttle. The mix shifts toward
-- the right column under load — more 2nd/3rd/4th = brighter, growlier engine.
local HarmonicAmpsIdle   = { 1.00, 0.40, 0.10, 0.04, 0.02 }
local HarmonicAmpsLoad   = { 1.00, 0.70, 0.30, 0.12, 0.05 }

local IdleNoiseAmp       = 0.04
local LoadNoiseAmp       = 0.18    -- exhaust rasp scales with throttle
local NoiseLPRate        = 0.18    -- 0..1, smaller = lower cutoff (more "thumpy")

local ParticleSystem1: SParticleSystemComponent = nil
local ParticleSystem2: SParticleSystemComponent = nil

-- --------------------------------------------------------------------------

local function CastWheel(self, BodyID, Origin, Up)
    local Settings = SRayCastSettings.new()
    Settings.Start = Origin
    Settings.End = Origin - Up * (RestLength + RayExtra)
    Settings.LayerMask = ECollisionProfiles.Static
    Settings.bDrawDebug = DebugWheels
    Settings.DebugDuration = 0.0
    Settings:AddIgnoredBody(BodyID)
    return self.World:CastRay(Settings)
end

-- Popup constants. Slots are pre-baked in the RML; the script rotates through
-- them, replacing each slot's inner RML to re-trigger its float-up animation.
local PopupSlotCount     = 8
local DriftPopupInterval = 0.45     -- seconds between mid-drift "+N" popups
local DriftPopupMinGain  = 25       -- skip popups below this so we don't spam
local DriftBigPopupAt    = 1500     -- mid-drift gain >= this gets the "bigwin" style
local DriftBankPopupMin  = 200      -- end-of-drift bank popup threshold

function VehicleMovement:OnReady()
    if not self:HasComponent(SProceduralAudioComponent) then
        self:AddComponent(SProceduralAudioComponent)
    end

    UI.LoadDocument(HUDDocument)
    self.HUDAccum = 0.0
    self.LastMPH  = -1
    self.LastRPM  = -1
    self.LastGear = ""
    self.LastBoostPct  = -1
    self.LastSlipPct   = 0
    self.LastSlipSide  = 0          -- -1 left, +1 right, 0 none
    self.LastBoostActive  = false
    self.LastBoostEmpty   = false
    self.LastSlipDrifting = false
    self.LastRPMRedline   = false
    self.LastDriftActive  = false
    self.LastDriftHuge    = false
    self.LastSpeedTier    = ""       -- "", "fast", "danger" — toggled on the mph gauge

    self.Boost         = 1.0        -- meter [0,1]
    self.DriftActive   = false
    self.DriftScore    = 0
    self.DriftMult     = 1.0
    self.DriftBanked   = 0          -- shown on banner
    self.TotalDrift    = 0
    self.DriftFade     = 0.0        -- post-drift display countdown

    -- Drift score change tracking so we can emit "+N" popups while drifting.
    self.DriftLastScoreSnapshot = 0
    self.DriftPopupAccum        = 0.0

    -- Popup ring. Slot index advances with each spawn so the same animation
    -- doesn't get clobbered mid-flight (8 slots * 1.4–1.8s anim = comfortable
    -- for any realistic event rate).
    self.PopupSlot = 0

    -- Crash banner state. CrashBannerFade counts down each frame; while > 0
    -- the banner is visible. CrashCooldownLeft suppresses follow-up hits from
    -- the same physical contact (Jolt fires a few enter events in quick
    -- succession when a body slides along a wall).
    self.CrashBannerFade  = 0.0
    self.CrashCooldownLeft = 0.0
    self.CrashLastAmount  = 0
    self.CrashLastHeavy   = false
    self.LastCrashActive  = false
    self.LastCrashHeavy   = false

    -- Run state
    self.TimeRemaining = RunDurationSeconds
    self.RaceFinished  = false
    self.TopSpeedMPH   = 0
    self.LastTimerStr  = ""
    self.LastTimerTier = ""                 -- "", "warn", "critical", "complete"
    self.RestartHooked = false

    ParticleSystem1 = self:GetComponentOf(44, SParticleSystemComponent)
    ParticleSystem2 = self:GetComponentOf(43, SParticleSystemComponent)

    local Audio = self:GetComponent(SProceduralAudioComponent)
    Audio.SampleRate   = AudioSampleRate
    Audio.ChannelCount = 1
    Audio.BufferFrames = AudioBufferFrames
    Audio.Volume       = 0.55
    Audio.Pitch        = 1.0
    Audio.MinDistance  = 2.0
    Audio.MaxDistance  = 90.0
    Audio.bSpatialized = true
    Audio:Start()

    self.Audio        = Audio
    self.RPM          = IdleRPM

    -- Per-harmonic phase accumulators (radians).
    self.Harm1Phase   = 0.0
    self.Harm2Phase   = 0.0
    self.Harm3Phase   = 0.0
    self.Harm4Phase   = 0.0
    self.Harm5Phase   = 0.0
    self.BurblePhase  = 0.0
    self.NoiseState   = 0.0  -- 1-pole LP filter memory for exhaust noise
end

function VehicleMovement:OnDetach()
    if self.Audio then
        self.Audio:Stop()
    end
end

-- Linear RPM ramp from idle to redline across the full speed range, blended
-- with throttle intent so flooring it at low speed still revs the engine.
local function UpdateEngineState(self, DeltaTime)
    local Speed    = vector.magnitude(self:GetLinearVelocity())
    local Throttle = math.abs(self.SmoothThrottle or 0.0)

    local SpeedT     = math.clamp(Speed / MaxSpeed, 0.0, 1.0)
    local SpeedRPM   = IdleRPM + (RedlineRPM - IdleRPM) * SpeedT
    local ThrottleRPM = IdleRPM + (RedlineRPM - IdleRPM) * Throttle
    local TargetRPM  = math.max(SpeedRPM, ThrottleRPM)

    local Rate  = (TargetRPM > self.RPM) and 6.0 or 3.0
    local Alpha = math.clamp(Rate * DeltaTime, 0.0, 1.0)
    self.RPM = self.RPM + (TargetRPM - self.RPM) * Alpha

    return self.RPM, Throttle
end

-- Linear-congruential noise; keeps state in `self` so successive frames don't
-- re-seed and create a tick at frame boundaries.
local function NextNoise(self)
    local S = (self.NoiseSeed or 0x12345) * 1103515245 + 12345
    S = S % 2147483648
    self.NoiseSeed = S
    return (S / 1073741824.0) - 1.0  -- in [-1, 1]
end

local function SynthEngineFrames(self, FrameCount, DeltaTime)
    local RPM, Throttle = UpdateEngineState(self, DeltaTime)

    -- Fundamental tracks crankshaft (RPM/60). Real V8 firing rate (×4) lands
    -- in shrill territory at high RPM; the lower fundamental + harmonic stack
    -- still reads as "engine" without piercing.
    local FiringHz   = RPM / 60.0
    local BurbleHz   = FiringHz * 0.5

    -- Crossfade idle → load harmonic mix by throttle.
    local Mix = math.clamp(Throttle, 0.0, 1.0)
    local A1  = HarmonicAmpsIdle[1] + (HarmonicAmpsLoad[1] - HarmonicAmpsIdle[1]) * Mix
    local A2  = HarmonicAmpsIdle[2] + (HarmonicAmpsLoad[2] - HarmonicAmpsIdle[2]) * Mix
    local A3  = HarmonicAmpsIdle[3] + (HarmonicAmpsLoad[3] - HarmonicAmpsIdle[3]) * Mix
    local A4  = HarmonicAmpsIdle[4] + (HarmonicAmpsLoad[4] - HarmonicAmpsIdle[4]) * Mix
    local A5  = HarmonicAmpsIdle[5] + (HarmonicAmpsLoad[5] - HarmonicAmpsIdle[5]) * Mix
    local NoiseAmp = IdleNoiseAmp + (LoadNoiseAmp - IdleNoiseAmp) * Mix

    -- More body at higher RPM so an idling engine doesn't sound as "full" as
    -- one screaming at redline.
    local RPMFactor = math.clamp((RPM - IdleRPM) / (RedlineRPM - IdleRPM), 0.0, 1.0)
    local Master    = 0.55 + 0.45 * RPMFactor

    local Inc1 = TWO_PI * FiringHz         / AudioSampleRate
    local Inc2 = TWO_PI * (FiringHz * 2.0) / AudioSampleRate
    local Inc3 = TWO_PI * (FiringHz * 3.0) / AudioSampleRate
    local Inc4 = TWO_PI * (FiringHz * 4.0) / AudioSampleRate
    local Inc5 = TWO_PI * (FiringHz * 5.0) / AudioSampleRate
    local IncB = TWO_PI * BurbleHz         / AudioSampleRate

    local P1, P2, P3, P4, P5, PB =
        self.Harm1Phase, self.Harm2Phase, self.Harm3Phase,
        self.Harm4Phase, self.Harm5Phase, self.BurblePhase
    local NoiseLP = self.NoiseState

    local Buf = buffer.create(FrameCount * 4)
    local sin = math.sin

    for i = 0, FrameCount - 1 do
        P1 = P1 + Inc1; if P1 > TWO_PI then P1 = P1 - TWO_PI end
        P2 = P2 + Inc2; if P2 > TWO_PI then P2 = P2 - TWO_PI end
        P3 = P3 + Inc3; if P3 > TWO_PI then P3 = P3 - TWO_PI end
        P4 = P4 + Inc4; if P4 > TWO_PI then P4 = P4 - TWO_PI end
        P5 = P5 + Inc5; if P5 > TWO_PI then P5 = P5 - TWO_PI end
        PB = PB + IncB; if PB > TWO_PI then PB = PB - TWO_PI end

        -- Cross-plane V8 burble: amplitude-modulate the fundamental at one
        -- crank-rev period (so half the firing rate). 0.55 leaves the
        -- harmonic audible at the modulation trough.
        local Burble = 0.55 + 0.45 * sin(PB)

        local Tone =
              sin(P1) * A1 * Burble
            + sin(P2) * A2
            + sin(P3) * A3
            + sin(P4) * A4
            + sin(P5) * A5

        -- Lowpass noise = exhaust rasp.
        local N = NextNoise(self)
        NoiseLP = NoiseLP + (N - NoiseLP) * NoiseLPRate
        Tone = Tone + NoiseLP * NoiseAmp

        local Sample = Tone * Master * 0.35  -- headroom; final volume on component
        if Sample > 1.0 then Sample = 1.0 elseif Sample < -1.0 then Sample = -1.0 end

        buffer.writef32(Buf, i * 4, Sample)
    end

    self.Harm1Phase, self.Harm2Phase, self.Harm3Phase = P1, P2, P3
    self.Harm4Phase, self.Harm5Phase, self.BurblePhase = P4, P5, PB
    self.NoiseState = NoiseLP
    return Buf
end

-- Push a floating score popup into the HUD's pre-baked slot ring. The slot's
-- inner RML is replaced with a fresh element carrying the popup-content class,
-- which re-triggers the float-up keyframe animation from frame 0.
function VehicleMovement:_PushPopup(Kind, Text)
    local Slot = self.PopupSlot or 0
    self.PopupSlot = (Slot + 1) % PopupSlotCount
    local Id = "popup-" .. tostring(Slot)
    UI.SetInnerRml(HUDDocument, Id,
        string.format('<div class="popup-content %s">%s</div>', Kind, Text))
end

-- Crash handling. The physics layer auto-invokes OnContactBegin whenever a
-- non-trigger contact starts on this entity's body; we deduct score based on
-- impact speed and arm the HUD crash banner.
function VehicleMovement:OnContactBegin(Event)
    -- Cooldown guard: a single car-vs-wall slide can fire several "enter"
    -- callbacks in quick succession as sub-shapes touch.
    if (self.CrashCooldownLeft or 0.0) > 0.0 then
        return
    end

    local Impact = Event.ImpactSpeed or 0.0
    if Impact < CrashMinImpact then
        return
    end

    local Penalty = math.floor(Impact * CrashScorePerImpact + 0.5)
    if Penalty < CrashScoreMin then Penalty = CrashScoreMin end
    if Penalty > CrashScoreMax then Penalty = CrashScoreMax end

    -- Take the penalty out of the banked drift total, then any active drift run.
    local Available = (self.TotalDrift or 0) + (self.DriftScore or 0)
    local Applied   = math.min(Penalty, Available)
    local Drained   = Applied
    if Drained > 0 then
        local FromTotal = math.min(self.TotalDrift or 0, Drained)
        self.TotalDrift = (self.TotalDrift or 0) - FromTotal
        Drained = Drained - FromTotal
    end
    if Drained > 0 then
        self.DriftScore = math.max(0.0, (self.DriftScore or 0) - Drained)
        self.DriftBanked = math.floor((self.DriftScore or 0) + 0.5)
    end

    self.CrashLastAmount  = Applied
    self.CrashLastHeavy   = Impact >= CrashHeavyImpact
    self.CrashBannerFade  = CrashBannerHold
    self.CrashCooldownLeft = CrashCooldown
    self.CrashLastImpactMPH = math.floor(Impact * MetersPerSecToMPH + 0.5)
    -- Re-arm the screen-flash overlay on every crash. Toggling the .active
    -- class off then on within the same frame doesn't re-trigger the
    -- animation, so we drive it off a state flag the HUD tick reads.
    self.CrashFlashPending = true

    -- Push the totals immediately (the throttled HUD path won't get to it for
    -- up to a frame and the banner needs to read the right value when it pops).
    UI.SetText(HUDDocument, "total-score", string.format("%.1f", self.TotalDrift))

    -- Floating popup at the impact spot (well, screen-anchored — we don't
    -- have world->screen projection in Lua right now). Only emit if the
    -- penalty was actually applied; if the player has 0 score, no popup.
    if Applied > 0 then
        self:_PushPopup("loss", string.format("-%d", Applied))
    end
end

-- Format seconds as M:SS.t (or 0:00.0 when negative). Tenths look better
-- than full ms for a HUD timer — easier to read at a glance under stress.
local function _FormatTimer(Seconds)
    if Seconds < 0.0 then Seconds = 0.0 end
    local Mins   = math.floor(Seconds / 60.0)
    local Rest   = Seconds - Mins * 60.0
    local SecInt = math.floor(Rest)
    local Tenth  = math.floor((Rest - SecInt) * 10.0)
    if Tenth > 9 then Tenth = 9 end
    return string.format("%d:%02d.%d", Mins, SecInt, Tenth)
end

function VehicleMovement:_RequestRestart()
    -- Engine.Travel is deferred (drained at next FrameStart), so it's safe to
    -- call from inside this script's tick — the world tear-down won't run
    -- until we've returned.
    Engine.Travel(LevelPath)
end

function VehicleMovement:_HookRestartButton()
    if self.RestartHooked then return end
    UI.OnEvent(HUDDocument, "endcard-restart", "click", function()
        self:_RequestRestart()
    end)
    self.RestartHooked = true
end

function VehicleMovement:_ShowEndScreen()
    UI.SetClass(HUDDocument, "endscreen", "visible", true)
    UI.SetText(HUDDocument, "endcard-headline", "TIME UP")
    UI.SetText(HUDDocument, "endcard-topspeed", string.format("%d MPH", self.TopSpeedMPH))
    UI.SetText(HUDDocument, "endcard-final-value", string.format("%.0f", self.TotalDrift or 0))
    -- Release the mouse so the player can click RESTART. Whichever script
    -- captured it during gameplay will re-arm it on the next level load.
    Input.SetMouseMode("Normal")
end

function VehicleMovement:OnUpdate(DeltaTime)
    -- Restart hotkey: reload the level via Engine.Travel. Available any time
    -- so the player can bail mid-run; level reload is deferred so the rest of
    -- this tick still runs to completion safely.
    self:_HookRestartButton()
    if Input.IsActionDown("Restart") then
        self:_RequestRestart()
        return
    end

    local BodyID   = self:GetBodyID()
    local Rotation = self:GetBodyRotation()
    local Loc      = self:GetBodyPosition()
    local Forward  = Rotation:RotateVector(vector.create(0.0, 0.0, 1.0))
    local Right    = Rotation:RotateVector(vector.create(1.0, 0.0, 0.0))
    local Up       = Rotation:RotateVector(vector.create(0.0, 1.0, 0.0))

    local RawThrottle = Input.GetActionAxis("MoveY")
    local Steer       = Input.GetActionAxis("MoveX")
    local Handbrake   = Input.IsActionDown("Jump")
    local Boost	      = Input.IsActionDown("Sprint")
    
    if Input.IsActionDown("Fire") then
        self:SetLocation(vector.create(0, 2, 0))
        self:SetRotationFromEuler(vector.zero)
    end
        
    if Handbrake then
    	ParticleSystem1:Activate(false)
		ParticleSystem2:Activate(false)
	end
	
	if Handbrake ~= bHandbreakLastFrame and not Handbrake then
		ParticleSystem1:Deactivate()
		ParticleSystem2:Deactivate()
	end
		
	bHandbreakLastFrame = Handbrake
	
    self.BoostBurning = false

    self.SmoothThrottle = self.SmoothThrottle or 0.0
    local Rate = (math.abs(RawThrottle) > math.abs(self.SmoothThrottle)) and ThrottleAttack or ThrottleRelease
    local TAlpha = math.clamp(Rate * DeltaTime, 0.0, 1.0)
    self.SmoothThrottle = self.SmoothThrottle + (RawThrottle - self.SmoothThrottle) * TAlpha
    local Throttle = self.SmoothThrottle

    local Wheels = {
        Loc + Forward *  HalfLength + Right *  HalfWidth + Up * WheelDrop,
        Loc + Forward *  HalfLength - Right *  HalfWidth + Up * WheelDrop,
        Loc - Forward *  HalfLength + Right *  HalfWidth + Up * WheelDrop,
        Loc - Forward *  HalfLength - Right *  HalfWidth + Up * WheelDrop,
    }

    local NumGrounded = 0
    for _, Origin in ipairs(Wheels) do
        local Hit = CastWheel(self, BodyID, Origin, Up)
        if Hit and Hit.Distance < RestLength then
            local Compression = RestLength - Hit.Distance
            local SpringSpeed = vector.dot(self:GetVelocityAtPoint(Origin), Up)
            local Force = SuspensionStiffness * Compression - SuspensionDamping * SpringSpeed
            if Force > 0.0 then
                Force = math.min(Force, MaxSuspensionForce)
                self:ApplyForceAt(Up * Force, Origin)
                NumGrounded = NumGrounded + 1
            end
        end
    end

    local Grounded = NumGrounded > 0
    
    local Velocity     = self:GetLinearVelocity()
    local ForwardSpeed = vector.dot(Velocity, Forward)

    if Grounded then
        if math.abs(Throttle) > 0.01 then
            local Accel
            if Throttle * ForwardSpeed < 0.0 and math.abs(ForwardSpeed) > 1.0 then
                Accel = math.sign(Throttle) * BrakeAccel
            elseif Throttle > 0.0 then
                Accel = Throttle * EngineAccel
            else
                Accel = Throttle * EngineAccel * ReverseFactor
            end
            local BoostBurning = Boost and (self.Boost or 0.0) > BoostMin and Throttle > 0.0
            if math.abs(ForwardSpeed) < MaxSpeed or Throttle * ForwardSpeed < 0.0 or BoostBurning then
                if BoostBurning then
                    Velocity = Velocity + Forward * (Accel * DeltaTime) * BoostMultiplier
                else
                    Velocity = Velocity + Forward * (Accel * DeltaTime)
                end
            end
            self.BoostBurning = BoostBurning
        else
            Velocity = Velocity - Forward * (ForwardSpeed * CoastDrag * DeltaTime)
        end

        local SideSpeed = vector.dot(Velocity, Right)
        local Grip      = Handbrake and DriftLateralGrip or LateralGrip
        local Alpha     = math.clamp(Grip * DeltaTime, 0.0, 1.0)
        Velocity = Velocity - Right * (SideSpeed * Alpha)
        self:SetVelocity(Velocity)
    end

    if Grounded then
        local SpeedFactor = math.clamp(math.abs(ForwardSpeed) / SteerSpeedFalloff, 0.0, 1.0)
        local YawTarget   = Steer * SteerYawRateMax * (0.4 + 0.6 * SpeedFactor)
        if ForwardSpeed < -0.5 then YawTarget = -YawTarget end

        local AngVel = self:GetAngularVelocity()
        local YawNow = vector.dot(AngVel, Up)
        local Blend  = math.clamp(SteerResponse * DeltaTime, 0.0, 1.0)
        local YawNew = YawNow + (YawTarget - YawNow) * Blend
        self:SetAngularVelocity(AngVel - Up * YawNow + Up * YawNew)
    end

    -- ----- Drift score / boost meter -----
    local CurVelocity   = self:GetLinearVelocity()
    local LateralSpeed  = vector.dot(CurVelocity, Right)
    local SignedSpeed   = vector.dot(CurVelocity, Forward)
    local SpeedAbs      = math.abs(SignedSpeed)

    if self.BoostBurning then
        self.Boost = math.max(0.0, (self.Boost or 0.0) - BoostDrainRate * DeltaTime)
    else
        self.Boost = math.min(1.0, (self.Boost or 0.0) + BoostRechargeRate * DeltaTime)
    end

    local IsDrifting = Grounded and Handbrake
                       and SpeedAbs > DriftMinSpeed
                       and math.abs(LateralSpeed) > DriftMinSlip

    if IsDrifting then
        if not self.DriftActive then
            self.DriftActive = true
            self.DriftMult   = 1.0
            self.DriftScore  = 0
            self.DriftLastScoreSnapshot = 0
            self.DriftPopupAccum = 0.0
        end
        self.DriftMult  = math.min(DriftMultMax, self.DriftMult + DriftMultRampUp * DeltaTime)
        local Gain      = math.abs(LateralSpeed) * SpeedAbs * self.DriftMult * DriftScoreRate * DeltaTime
        self.DriftScore = self.DriftScore + Gain
        self.DriftBanked = math.floor(self.DriftScore + 0.5)
        self.DriftFade  = DriftBankFade

        -- Periodic mid-drift "+N" popup so each chunk of score has a visible
        -- payoff. Snapshot diffing keeps the values honest even when the
        -- accumulator overshoots (long frame, etc.).
        self.DriftPopupAccum = (self.DriftPopupAccum or 0.0) + DeltaTime
        if self.DriftPopupAccum >= DriftPopupInterval then
            self.DriftPopupAccum = 0.0
            local IntervalGain = math.floor((self.DriftScore - self.DriftLastScoreSnapshot) + 0.5)
            if IntervalGain >= DriftPopupMinGain then
                local Kind = (IntervalGain >= DriftBigPopupAt) and "bigwin" or "gain"
                self:_PushPopup(Kind, string.format("+%d", IntervalGain))
            end
            self.DriftLastScoreSnapshot = self.DriftScore
        end
    elseif self.DriftActive then
        self.DriftActive = false
        local Banked = math.floor(self.DriftScore + 0.5)
        self.Boost = math.min(1.0, (self.Boost or 0.0) + self.DriftScore * DriftBoostReward)
        self.TotalDrift += self.DriftScore
        self.DriftScore = 0
        self.DriftLastScoreSnapshot = 0

        -- "BANK +N" popup when ending a meaningful drift run. This is what the
        -- player feels as the payoff for committing to a long slide.
        if Banked >= DriftBankPopupMin then
            self:_PushPopup("banked", string.format("BANK +%d", Banked))
        end
    elseif self.DriftFade > 0.0 then
        self.DriftFade = math.max(0.0, self.DriftFade - DeltaTime)
    end

    -- ----- Drift sprint timer -----
    -- Top-speed tracking — used in the end-of-run summary.
    do
        local CurMPH = math.floor(math.abs(SignedSpeed) * MetersPerSecToMPH + 0.5)
        if CurMPH > (self.TopSpeedMPH or 0) then
            self.TopSpeedMPH = CurMPH
        end
    end

    if not self.RaceFinished then
        self.TimeRemaining = self.TimeRemaining - DeltaTime
        if self.TimeRemaining <= 0.0 then
            self.TimeRemaining = 0.0
            self.RaceFinished  = true
            self:_ShowEndScreen()
        end
    end

    self.HUDAccum = (self.HUDAccum or 0.0) + DeltaTime
    if self.HUDAccum >= 1.0 / HUDUpdateRate then
        self.HUDAccum = 0.0

        local MPH = math.floor(math.abs(ForwardSpeed) * MetersPerSecToMPH + 0.5)
        local RPM = math.floor((self.RPM or IdleRPM) + 0.5)
        local Gear
        if ForwardSpeed < -0.5 then
            Gear = "R"
        elseif math.abs(ForwardSpeed) < 0.5 and math.abs(Throttle) < 0.05 then
            Gear = "N"
        else
            Gear = "D"
        end

        if MPH ~= self.LastMPH then
            UI.SetText(HUDDocument, "mph-value", tostring(MPH))
            self.LastMPH = MPH
        end
        if RPM ~= self.LastRPM then
            UI.SetText(HUDDocument, "rpm-value", tostring(RPM))
            -- RPM fill bar: 0..1 blend across IdleRPM..RedlineRPM. The fill is
            -- a fresh element each update so the gradient decorator re-evaluates
            -- against the new width. Skip the fill entirely below 1% — RmlUi's
            -- gradient decorator can't generate element data on a 0-width box
            -- and logs a warning every frame.
            local RPMFrac = math.clamp((self.RPM - IdleRPM) / (RedlineRPM - IdleRPM), 0.0, 1.0)
            local RPMPct  = math.floor(RPMFrac * 100.0 + 0.5)
            if RPMPct < 1 then
                UI.SetInnerRml(HUDDocument, "rpm-bar", "")
            else
                UI.SetInnerRml(HUDDocument, "rpm-bar",
                    string.format('<div id="rpm-fill" style="width: %d%%;"/>', RPMPct))
            end
            self.LastRPM = RPM
        end
        if Gear ~= self.LastGear then
            UI.SetText(HUDDocument, "gear-value", Gear)
            self.LastGear = Gear
        end

        -- Speed-tier styling on the MPH gauge: cyan glow once you're moving
        -- with conviction, golden pulse once you're at top-end. Anything
        -- below ~30 mph stays neutral white so daily-driver speeds don't
        -- look artificially loud.
        local SpeedTier = ""
        if MPH >= 95 then
            SpeedTier = "danger"
        elseif MPH >= 35 then
            SpeedTier = "fast"
        end
        if SpeedTier ~= self.LastSpeedTier then
            UI.SetClass(HUDDocument, "mph", "fast",   SpeedTier == "fast")
            UI.SetClass(HUDDocument, "mph", "danger", SpeedTier == "danger")
            self.LastSpeedTier = SpeedTier
        end

        local BoostPct = math.floor((self.Boost or 0.0) * 100.0 + 0.5)
        if BoostPct ~= self.LastBoostPct then
            -- Skip the fill div entirely at 0% — gradient decorators can't
            -- compute against a zero-width box (RmlUi logs a warning per frame).
            if BoostPct < 1 then
                UI.SetInnerRml(HUDDocument, "boost-track", "")
            else
                UI.SetInnerRml(HUDDocument, "boost-track",
                    string.format('<div id="boost-fill" style="width: %d%%;"/>', BoostPct))
            end
            UI.SetText(HUDDocument, "boost-pct", string.format("%d%%", BoostPct))
            self.LastBoostPct = BoostPct
        end
        local BoostActive = self.BoostBurning == true
        local BoostEmpty  = (not BoostActive) and ((self.Boost or 0.0) <= BoostMin)
        if BoostActive ~= self.LastBoostActive then
            UI.SetClass(HUDDocument, "boost-panel", "active", BoostActive)
            self.LastBoostActive = BoostActive
        end
        if BoostEmpty ~= self.LastBoostEmpty then
            UI.SetClass(HUDDocument, "boost-panel", "empty", BoostEmpty)
            self.LastBoostEmpty = BoostEmpty
        end

        local SlipNorm = math.clamp(LateralSpeed / 12.0, -1.0, 1.0)
        local SlipPct  = math.floor(math.abs(SlipNorm) * 50.0 + 0.5)
        local SlipSide = (SlipNorm > 0.05) and 1 or ((SlipNorm < -0.05) and -1 or 0)
        if SlipPct ~= self.LastSlipPct or SlipSide ~= self.LastSlipSide then
            local LeftPct  = (SlipSide < 0) and SlipPct or 0
            local RightPct = (SlipSide > 0) and SlipPct or 0
            UI.SetInnerRml(HUDDocument, "slip-track", string.format('<div id="slip-left" style="width: %d%%;"/><div id="slip-right" style="width: %d%%;"/>', LeftPct, RightPct))
            self.LastSlipPct  = SlipPct
            self.LastSlipSide = SlipSide
        end
        local SlipDrifting = self.DriftActive == true
        if SlipDrifting ~= self.LastSlipDrifting then
            UI.SetClass(HUDDocument, "slip-track", "drifting", SlipDrifting)
            self.LastSlipDrifting = SlipDrifting
        end

        local RPMRedline = RPM >= RedlineRPM * RedlineWarn
        if RPMRedline ~= self.LastRPMRedline then
            UI.SetClass(HUDDocument, "rpm", "redline", RPMRedline)
            self.LastRPMRedline = RPMRedline
        end

        local DriftBannerActive = self.DriftActive or self.DriftFade > 0.0
        local DriftBannerHuge   = DriftBannerActive and self.DriftBanked >= DriftHugeThreshold
        if DriftBannerActive ~= self.LastDriftActive then
            UI.SetClass(HUDDocument, "drift-banner", "active", DriftBannerActive)
            self.LastDriftActive = DriftBannerActive
        end
        if DriftBannerHuge ~= self.LastDriftHuge then
            UI.SetClass(HUDDocument, "drift-banner", "huge", DriftBannerHuge)
            self.LastDriftHuge = DriftBannerHuge
        end
        if DriftBannerActive then
            UI.SetText(HUDDocument, "drift-score", tostring(self.DriftBanked))
            UI.SetText(HUDDocument, "drift-mult", string.format("x%.1f", self.DriftMult))
            UI.SetText(HUDDocument, "total-score", string.format("%.1f", self.TotalDrift))
        end

        local CrashActive = (self.CrashBannerFade or 0.0) > 0.0
        local CrashHeavy  = CrashActive and (self.CrashLastHeavy == true)
        if CrashActive then
            UI.SetText(HUDDocument, "crash-amount", string.format("-%d", self.CrashLastAmount or 0))
            UI.SetText(HUDDocument, "crash-impact", string.format("%d mph impact", self.CrashLastImpactMPH or 0))
        end
        if CrashActive ~= self.LastCrashActive then
            UI.SetClass(HUDDocument, "crash-banner", "active", CrashActive)
            self.LastCrashActive = CrashActive
        end
        if CrashHeavy ~= self.LastCrashHeavy then
            UI.SetClass(HUDDocument, "crash-banner", "heavy", CrashHeavy)
            self.LastCrashHeavy = CrashHeavy
        end

        -- Run-timer HUD writes
        local TimerStr = _FormatTimer(self.TimeRemaining or 0.0)
        if TimerStr ~= self.LastTimerStr then
            UI.SetText(HUDDocument, "timer-value", TimerStr)
            self.LastTimerStr = TimerStr
        end
        local TimerTier = ""
        if self.RaceFinished then
            TimerTier = "complete"
        elseif self.TimeRemaining <= TimerCriticalAt then
            TimerTier = "critical"
        elseif self.TimeRemaining <= TimerWarnAt then
            TimerTier = "warn"
        end
        if TimerTier ~= self.LastTimerTier then
            UI.SetClass(HUDDocument, "timetrial-panel", "warn",     TimerTier == "warn")
            UI.SetClass(HUDDocument, "timetrial-panel", "critical", TimerTier == "critical")
            UI.SetClass(HUDDocument, "timetrial-panel", "complete", TimerTier == "complete")
            self.LastTimerTier = TimerTier
        end
    end

    -- Crash banner / cooldown countdowns run every frame so the HUD throttle
    -- doesn't introduce visible lag between the impact and the banner showing.
    if (self.CrashBannerFade or 0.0) > 0.0 then
        self.CrashBannerFade = math.max(0.0, self.CrashBannerFade - DeltaTime)
    end
    if (self.CrashCooldownLeft or 0.0) > 0.0 then
        self.CrashCooldownLeft = math.max(0.0, self.CrashCooldownLeft - DeltaTime)
    end

    -- Crash-flash overlay: re-trigger the keyframe by replacing the element
    -- entirely (toggling .active off/on within a frame doesn't restart the
    -- animation). Cleared by SetInnerRml on the parent body? No — the flash
    -- div is a sibling. We swap its outer wrapper instead via SetClass off,
    -- and arm via SetClass on next tick. Two-stage so the class change is
    -- observed: clear on this tick, set on the next.
    if self.CrashFlashPending then
        if self.CrashFlashStage == nil then
            UI.SetClass(HUDDocument, "crash-flash", "active", false)
            self.CrashFlashStage = "armed"
        elseif self.CrashFlashStage == "armed" then
            UI.SetClass(HUDDocument, "crash-flash", "active", true)
            self.CrashFlashPending = false
            self.CrashFlashStage   = nil
        end
    end


    -- Top up the procedural audio ring buffer so the audio thread never starves.
    if self.Audio then
        local Queued = self.Audio:GetQueuedFrameCount()
        local Free   = self.Audio:GetFreeFrameCount()
        local Want   = AudioTargetQueued - Queued
        if Want > 0 and Free > 0 then
            local FrameCount = math.min(Want, Free)
            self.Audio:QueueSamples(SynthEngineFrames(self, FrameCount, DeltaTime))
        end
    end
end

return VehicleMovement
