Mainmenu = { }

local MENU = "/Game/Widgets/MainMenu.rml"

function Mainmenu:OnReady(entity)
    UI.LoadDocument(MENU)
    Input.SetMode("UI")
    Input.SetMouseMode("Normal")

    UI.OnEvent(MENU, "item-play", "click", function()
        Engine.Travel("/Game/Levels/Level_01")
    end)

    UI.OnEvent(MENU, "item-settings", "click", function()
        UI.LoadDocument("/Game/Widgets/Settings.rml")
    end)

    UI.OnEvent(MENU, "item-quit", "click", function()
    end)
end

function Mainmenu:OnDestroy(entity)
    -- Optional. World teardown unloads the doc anyway.
end

return Mainmenu