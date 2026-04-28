NewScript = { }

local DOC = "/Game/TestWidget.rml"

function NewScript:OnAttach()
end

function NewScript:OnReady()
    local clicks = 0
    local debugger_visible = false
    local menu_open = false

    local function refresh_counter()
        UI.SetText(DOC, "counter-value", tostring(clicks))
    end

    local function open_menu()
        if menu_open then return true end
        if not UI.LoadDocument(DOC) then
            print("[UI] Failed to load " .. DOC)
            return false
        end
        Input.SetMode("UI")
        Input.SetMouseMode("Normal")
        refresh_counter()
        UI.SetText(DOC, "diag", "Input routed to UI. Click +1, Reset, or Close menu.")
        menu_open = true

        local function close_menu()
            if not menu_open then return end
                UI.UnloadDocument(DOC)
                Input.SetMode("Game")
                Input.SetMouseMode("Captured")
                menu_open = false
                print("[UI] Menu closed; input mode -> " .. Input.GetMode())
        end

        UI.OnEvent(DOC, "bump",  "click", function() clicks = clicks + 1; refresh_counter() end)
        UI.OnEvent(DOC, "reset", "click", function() clicks = 0; refresh_counter() end)
        UI.OnEvent(DOC, "toggle-debugger", "click", function()
            debugger_visible = not debugger_visible
            UI.SetDebuggerVisible(debugger_visible)
        end)
        UI.OnEvent(DOC, "close", "click", close_menu)

        return true
    end

    -- Boot in gameplay mode. The menu is opt-in via the Pause action.
    Input.SetMode("Game")
    Input.SetMouseMode("Captured")

    Input.OnActionPressed("Pause", function()
        if menu_open then close_menu() else open_menu() end
    end)

    Input.OnActionPressed("Fire", function()
        print("[Game] Fire!")
    end)
end

local MOUSE_SENSITIVITY = 0.15
local PITCH_MIN         = -89
local PITCH_MAX         = 89

local WALK_SPEED        = 5.0
local SPRINT_SPEED      = 15.0

function NewScript:Update(DeltaTime: number)
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

function NewScript:OnDetach()
end

return NewScript
