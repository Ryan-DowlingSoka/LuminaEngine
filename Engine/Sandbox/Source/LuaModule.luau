local Script: EntityScript = EntityScript.new()

local PlayerEntity = 1048580
local PlayerTransform: STransformComponent? = nil

function Script:OnUpdate(DeltaTime: number)
    if PlayerTransform == nil then
        PlayerTransform = self:GetComponentOf(PlayerEntity, STransformComponent)
    end

    self:SetLocation(PlayerTransform:GetLocation() + vector.create(0.0, 1.0, 0.0))

    local MouseX = Input.GetMouseDeltaX() * 0.1
    local MouseY = Input.GetMouseDeltaY() * 0.1

    self.Transform:AddYaw(-MouseX)
    self.Transform:AddPitch(MouseY, -89.9, 89.9)
end

return Script
