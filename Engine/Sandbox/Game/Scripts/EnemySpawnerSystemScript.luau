
NewScript = { }

NewScript.__meta = {
    TickRate = 0.2
}


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
    -- Called when the entity hierarchy is initialized
end

function NewScript:Update(DeltaTime: number)
    local Enemy = World:GetEntityByTag("Enemy") -- Will just find the first one.
    local Location = Registry:Get(Enemy, STransformComponent):GetLocation()

    --Registry:Duplicate(Enemy)

    --Engine.Audio.PlaySoundAtLocation("/Game/Content/Spawn.wav", Location)

end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
