
NewScript = {
 }

 NewScript._meta = {
    TickRate = 1.5,
 }


function NewScript:OnAttach()
    -- Called when the entity is created
end

function NewScript:OnReady()
    local Thingy = CMaterial.Load("/Game/Content/M_Enemy")
end

function NewScript:Update(DeltaTime: number)

end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
