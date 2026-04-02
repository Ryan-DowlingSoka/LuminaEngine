
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
    self.Transform:SetLocation(PlayerTransform:GetLocation() + vector.create(0.0, 0.0, -20.0))
end

function NewScript:Update(DeltaTime: number)
    
end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
