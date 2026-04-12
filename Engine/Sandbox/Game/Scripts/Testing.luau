
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

    if World:GetNumEntities() > 1000000 then
        return
    end

    for i = 1, 10000 do
        local Entity = Registry:Duplicate(11)
        local Transform = Registry:Get(Entity, STransformComponent)
        Transform:SetLocation(vector.create(math.random(-1000, 1000), 0.0, math.random(-1000, 1000)))
    end
end
            
function NewScript:OnDetach()
    -- Called when the entity is destroyed.
end


return NewScript
