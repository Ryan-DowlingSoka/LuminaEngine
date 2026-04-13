
NewScript = {
 }

 NewScript._meta = {
    TickRate = 1.5,
 }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
end

function NewScript:Update(DeltaTime: number)

    for i = 1, 1 do
        Registry:Get(Registry:Duplicate(15), STransformComponent):SetLocation(vector.create(math.random(-1000, 1000), 0.0, math.random(-1000, 1000)))
    end
end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
