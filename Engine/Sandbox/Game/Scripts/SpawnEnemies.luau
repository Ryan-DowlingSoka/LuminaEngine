
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()

        for i = 1, 50000 do
            local Entity = Registry:Duplicate(4)
            Engine.ECS.TranslateEntity(Registry, Entity, vector.create(math.random(-5000, 5000), math.random(50, 1500), math.random(-5000, 5000)))
        end

end

function NewScript:Update(DeltaTime: number)


end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
