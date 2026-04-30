local Script = EntityScript.new()

local DOC = "/Game/Widgets/MainMenu.rml"

local MOUSE_SENSITIVITY = 0.15
local PITCH_MIN         = -89
local PITCH_MAX         = 89

local WALK_SPEED        = 5.0
local SPRINT_SPEED      = 15.0

function Script:OnReady()
    UI.LoadDocument(DOC)
    Input.SetMode("UI")
    Input.SetMouseMode("Normal")
end

function Script:Update(DeltaTime: number)
    -- Camera look uses raw mouse deltas. The polling layer's UI-mode gate
    -- zeroes these automatically while a menu is open, so no explicit
    -- "is the menu up?" check is needed here.
    local MouseDX = Input.GetMouseDeltaX()
    local MouseDY = -Input.GetMouseDeltaY()

    if MouseDX ~= 0 then
        self.Transform:AddYaw(MouseDX * MOUSE_SENSITIVITY)
    end
    if MouseDY ~= 0 then
        self.Transform:AddPitch(-MouseDY * MOUSE_SENSITIVITY, PITCH_MIN, PITCH_MAX)
    end

    local InputX = Input.GetActionAxis("MoveX")
    local InputZ = Input.GetActionAxis("MoveY")

    if InputX == 0 and InputZ == 0 then return end

    local Length = math.sqrt(InputX * InputX + InputZ * InputZ)
    InputX = InputX / Length
    InputZ = InputZ / Length

    local Speed = Input.IsActionDown("Sprint") and SPRINT_SPEED or WALK_SPEED

    local Forward = self.Transform:GetForward()
    local Right   = self.Transform:GetRight()
    local Move    = (Forward * InputZ) + (Right * InputX)

    self.Transform:Translate(Move * (Speed * DeltaTime))
end

return Script
