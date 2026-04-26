
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()

end

function NewScript:Update(DeltaTime: number)

        for i = 1, 100 do
            local Entity = Registry:Duplicate(2)
            Engine.ECS.TranslateEntity(Registry, Entity, vector.create(math.random(-5000, 5000), math.random(50, 1500), math.random(-5000, 5000)))

            local Random = math.random(1, 5)
            Engine.ECS.SetEntityScale(Registry, Entity, vector.create(Random, Random, Random))
        end
end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
