
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()

        for i = 1, 200000 do
            local Entity = Registry:Duplicate(5)
            Engine.ECS.TranslateEntity(Registry, Entity, vector.create(math.random(-500, 500), math.random(50, 150), math.random(-500, 500)))
        end

end

function NewScript:Update(DeltaTime: number)


end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
