
NewScript = { }


function NewScript:OnAttach()
    
end

function NewScript:OnReady()
    -- Called when the entity hierarchy is initialized
end

function NewScript:Update(DeltaTime: number)
    
    if Input.IsKeyDown(EKey.W) then
        self.Transform:Translate(vector.create(0.0, 1 * DeltaTime, 0.0))
    end

    if Input.IsKeyDown(EKey.S) then
        self.Transform:Translate(vector.create(0.0, -1 * DeltaTime, 0.0))
    end

    if Input.IsKeyDown(EKey.A) then
        self.Transform:Translate(vector.create(1 * DeltaTime, 0.0, 0.0))
    end

    if Input.IsKeyDown(EKey.D) then
        self.Transform:Translate(vector.create(-1 * DeltaTime, 0.0, 0.0))
    end

    Registry:DispatchEvent(SImpulseEvent.new())

end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
