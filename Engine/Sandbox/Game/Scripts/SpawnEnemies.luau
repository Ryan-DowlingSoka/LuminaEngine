
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
        for i = 1, 90900 do
        local Entity = 1048578
        local Duplicate = Registry:Duplicate(Entity)
        Engine.ECS.TranslateEntity(Registry, Duplicate, vector.create(math.random(-2000, 2000), math.random(-1, 2000), math.random(-2000, 2000)))
    end
end

function NewScript:Update(DeltaTime: number)


end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
