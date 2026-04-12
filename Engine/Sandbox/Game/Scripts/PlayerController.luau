-- Do not use these example/testing scripts as a reference. They will have bad habbits.

local NewScript = {}


local CharacterControllerComponent: SCharacterControllerComponent?
local CharacterPhysicsComponent: SCharacterPhysicsComponent?
local CharacterMovementComponent: SCharacterMovementComponent?


local function Shoot(Script)

    Engine.Audio.PlaySoundAtLocation("/Game/Content/RayGun.wav", Script.Transform:GetLocation())

    local RaySettings: SRayCastSettings = SRayCastSettings.new()
    RaySettings.DebugDuration   = 1.0
    RaySettings.bDrawDebug      = true
    RaySettings.Start           = Script.Transform:GetLocation() + vector.create(0.0, 1.0, 0.0)
    RaySettings.End             = RaySettings.Start + CharacterControllerComponent:GetLookForward() * 500.0
    RaySettings.LayerMask       = ECollisionProfiles.Channel0

    local HitResult: SRayResult? = World:CastRay(RaySettings)
    if HitResult ~= nil then
        local Health: SHealthComponent? = Registry:Get(HitResult.Entity, SHealthComponent)
        if Health then
            if Health:ApplyDamage(50) < 0.0 then
                Registry:Destroy(HitResult.Entity)
            end
        end
    end

end

function NewScript:OnAttach()
end

function NewScript:OnReady()
    CharacterControllerComponent    = World:GetEntityRegistry():Get(self.Entity, SCharacterControllerComponent)
    CharacterPhysicsComponent       = World:GetEntityRegistry():Get(self.Entity, SCharacterPhysicsComponent)
    CharacterMovementComponent      = World:GetEntityRegistry():Get(self.Entity, SCharacterMovementComponent)

    --Input.SetMouseMode(EMouseMode.Captured)
end

function NewScript:Update(DeltaTime: number)

    local X: number = 0
    local Y: number = 0

    if Input.IsKeyDown(EKey.W) then Y += 1.0 end
    if Input.IsKeyDown(EKey.S) then Y -= 1.0 end
    if Input.IsKeyDown(EKey.A) then X += 1.0 end
    if Input.IsKeyDown(EKey.D) then X -= 1.0 end

    if Input.IsKeyDown(EKey.LeftShift) then
        CharacterMovementComponent.MoveSpeed = 30.0
    else
        CharacterMovementComponent.MoveSpeed = 10.0
    end

    if Input.IsKeyDown(EKey.Space) then
        CharacterControllerComponent:Jump()
    end


    local MovementInput = vector.create(X, 0.0, Y)
    CharacterControllerComponent:AddMovementInput(MovementInput)

    local MouseX = Input.GetMouseDeltaX() * 0.1
    local MouseY = Input.GetMouseDeltaY() * 0.1

    CharacterControllerComponent:AddLookInput(vector.create(-MouseX, MouseY))

    if Input.IsMouseButtonReleased(EMouseKey.ButtonLeft) then
       Shoot(self) 
    end

end

function NewScript:OnDetach()
end

return NewScript
