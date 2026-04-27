-- Demo: pause-menu open/close pattern using the new input routing.
--
-- Open path:  load doc, save previous Input mode, switch to "UI" so clicks
--             reach RmlUi, free the cursor.
-- Close path: restore the previous Input mode + mouse mode, unload the doc.
--
-- The same script works in editor preview and PIE: UI.LoadDocument always
-- targets the active world's context.

local DOC = "/Engine/Resources/UI/Documents/TestWidget.rml"

local clicks = 0
local debugger_visible = false
local previous_input_mode = nil
local previous_mouse_mode = nil

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

    -- Free the cursor so the user can actually click. Mouse mode is
    -- orthogonal to input mode in Lumina, so set it explicitly.
    previous_mouse_mode = "Captured"
    Input.SetMouseMode("Normal")

    refresh_counter()
    UI.SetText(DOC, "diag",
        "Input routed to UI. Click +1, Reset, or Close menu.")
    return true
end

local function close_menu()
    UI.UnloadDocument(DOC)

    -- Hand input back to the game and re-capture the cursor.
    Input.SetMode(previous_input_mode or "Game")
    Input.SetMouseMode(previous_mouse_mode or "Captured")
    previous_input_mode = nil
    previous_mouse_mode = nil

    print("[UI] Menu closed; input mode -> " .. Input.GetMode())
end

function NewScript:OnReady()
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
