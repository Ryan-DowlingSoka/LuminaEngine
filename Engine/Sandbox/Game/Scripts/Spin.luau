
NewScript = { }

local TimeScale = 0.0

function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
    TimeScale = math.random(5.5, 15.0)
end

function NewScript:Update(DeltaTime: number)
    
    local Spin = vector.create(0.0, TimeScale * DeltaTime, 0.0)
    self.Transform:AddRotationFromEuler(Spin)
end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
