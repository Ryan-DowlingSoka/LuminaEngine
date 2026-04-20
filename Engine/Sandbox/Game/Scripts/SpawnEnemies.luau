
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
        for i = 1, 10000 do
        local Entity = 2
        local Duplicate = Registry:Duplicate(Entity)
        Engine.ECS.TranslateEntity(Registry, Duplicate, vector.create(math.random(-100, 100), math.random(-1, 100), math.random(-100, 100)))
    end
end

function NewScript:Update(DeltaTime: number)


end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
