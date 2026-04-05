
NewScript = { }


function NewScript:OnAttach()
    
end

function NewScript:OnReady()
    -- Called when the entity hierarchy is initialized
    Input.SetMouseMode(EMouseMode.Captured)
end

function NewScript:Update(DeltaTime: number)
    
    local Forward = self.Transform:GetUp()
    local Right = self.Transform:GetRight()

    local Speed = 10

    if Input.IsKeyDown(EKey.W) then
        self.Transform:Translate(-Forward * DeltaTime * Speed)
    end

    if Input.IsKeyDown(EKey.S) then
        self.Transform:Translate(Forward * DeltaTime * Speed)
    end

    
    local MouseX = Input.GetMouseDeltaX() * DeltaTime * 10
    local MouseY = Input.GetMouseDeltaY() * DeltaTime * 10
    self.Transform:AddYaw(-MouseX)
    self.Transform:AddPitch(MouseY, -89, 89)

    --Registry:DispatchEvent(SImpulseEvent.new())

end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
