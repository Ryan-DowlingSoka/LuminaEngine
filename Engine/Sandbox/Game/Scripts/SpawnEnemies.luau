
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()

        for i = 1, 1000 do
            World:SpawnPrefabAsync("/Game/Content/NewPrefab", function(SpawnedEntity)
                if SpawnedEntity then
                    Engine.ECS.TranslateEntity(Registry, SpawnedEntity, vector.create(math.random(-50, 50), math.random(-1, 50), math.random(-50, 50)))
                end
            end)
        end

        for i = 1, 50000 do
            local Entity = Registry:Duplicate(2)
            Engine.ECS.TranslateEntity(Registry, Entity, vector.create(math.random(-500, 500), math.random(50, 150), math.random(-500, 500)))
        end

end

function NewScript:Update(DeltaTime: number)


end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
