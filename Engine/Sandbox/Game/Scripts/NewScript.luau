
NewScript = { }


function NewScript:OnAttach()
    -- Called when the entity is created
end

local DOC = "/Engine/Resources/UI/Documents/TestWidget.rml"

function NewScript:OnReady()
    local clicks = 0
    local debugger_visible = false
    local previous_input_mode = nil

    local function refresh_counter()
        UI.SetText(DOC, "counter-value", tostring(clicks))
    end

    local function open_menu()
        if not UI.LoadDocument(DOC) then
            print("[UI] Failed to load " .. DOC)
            return false
        end

        previous_input_mode = Input.GetMode()
        Input.SetMode("UI")
        Input.SetMouseMode("Normal")

        refresh_counter()
        UI.SetText(DOC, "diag",
            "Input routed to UI. Click +1, Reset, or Close menu.")
        return true
    end

    local function close_menu()
        UI.UnloadDocument(DOC)
        Input.SetMode(previous_input_mode or "Game")
        Input.SetMouseMode("Captured")
        previous_input_mode = nil
        print("[UI] Menu closed; input mode -> " .. Input.GetMode())
    end

    if not open_menu() then return end

    UI.OnEvent(DOC, "bump", "click", function()
        clicks = clicks + 1
        refresh_counter()
        print("[UI] +1 -> " .. tostring(clicks))
    end)

    UI.OnEvent(DOC, "reset", "click", function()
        clicks = 0
        refresh_counter()
        print("[UI] reset")
    end)

    UI.OnEvent(DOC, "toggle-debugger", "click", function()
        debugger_visible = not debugger_visible
        UI.SetDebuggerVisible(debugger_visible)
    end)

    UI.OnEvent(DOC, "close", "click", close_menu)

    print(UI.DescribeState())
end

local MoveSpeed         = 5.0   -- units/sec
local MouseSensitivity  = 0.15  -- degrees per pixel of mouse delta
local PitchMin          = -89   -- degrees; just shy of straight-down to avoid gimbal flip
local PitchMax          = 89    -- degrees; just shy of straight-up

function NewScript:Update(DeltaTime: number)
    
    --Input.SetMouseMode(EMouseMode.Captured)

    local MouseDeltaX = Input.GetMouseDeltaX()
    local MouseDeltaY = -Input.GetMouseDeltaY()

    if MouseDeltaX ~= 0 then
        -- Negate so mouse-right yaws clockwise (looking from above), the
        -- standard FPS feel. Flip the sign if your input axis is inverted.
        self.Transform:AddYaw(-MouseDeltaX * MouseSensitivity)
    end

    if MouseDeltaY ~= 0 then
        -- AddPitch clamps internally so we can't roll past the poles.
        self.Transform:AddPitch(-MouseDeltaY * MouseSensitivity, PitchMin, PitchMax)
    end

    -- Collect WASD into a 2D input vector. Strafe on X, forward on Z.
    local InputX = 0
    local InputZ = 0

    if Input.IsKeyDown(EKey.W) then InputZ = InputZ + 1 end
    if Input.IsKeyDown(EKey.S) then InputZ = InputZ - 1 end
    if Input.IsKeyDown(EKey.D) then InputX = InputX + 1 end
    if Input.IsKeyDown(EKey.A) then InputX = InputX - 1 end

    if InputX == 0 and InputZ == 0 then
        return
    end

    -- Normalize the 2D input so diagonal movement (W+D etc.) isn't sqrt(2)
    -- faster than a single axis.
    local InputLength = math.sqrt(InputX * InputX + InputZ * InputZ)
    InputX = InputX / InputLength
    InputZ = InputZ / InputLength

    -- Translate along the character's current facing. Forward and right are
    -- already unit vectors, so the resulting move vector has length 1.
    local Forward = self.Transform:GetForward()
    local Right   = self.Transform:GetRight()
    local Move    = (Forward * InputZ) + (Right * InputX)

    self.Transform:Translate(Move * (MoveSpeed * DeltaTime))
end
            
function NewScript:OnDetach()
end


return NewScript
