
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
    print("This is a test")
end

function NewScript:Update(DeltaTime: number)
    -- Called every frame.
end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
