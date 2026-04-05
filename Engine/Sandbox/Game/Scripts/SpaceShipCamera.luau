
NewScript = { }

local Player = nil
local PlayerTransform: STransformComponent?
local DesiredFollowLocation: vector

function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
    Player = World:GetEntityByTag("Player")
    PlayerTransform = Registry:Get(Player, STransformComponent)
end

function NewScript:Update(DeltaTime: number)
        local PlayerLocation = PlayerTransform:GetLocation()
        self.Transform:SetLocation(PlayerLocation + vector.create(0.0, 1.0, -5.0))
        

end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
