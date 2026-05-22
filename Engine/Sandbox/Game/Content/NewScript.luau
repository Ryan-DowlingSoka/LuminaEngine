-- Per-world UI test suite. Exercises the UI.* Lua module against this world's
-- own FWorldUIContext: load/unload, show/hide, SetText/SetInnerRml/SetClass,
-- OnEvent click handlers, the debugger toggle, and DescribeState.

local UITestScript = EntityScript.new()

local HUD = "/Game/TestHUD.rml"
local POPUP = "/Game/TestPopup.rml"

function UITestScript:OnReady()
	self.Frames = 0
	self.Elapsed = 0.0
	self.Counter = 0
	self.Highlighted = false
	self.PopupOpen = false
	self.DebuggerOn = false

	-- Opt into UI input: worlds default to EInputMode::Game (no UI routing) so
	-- mouse-look isn't hijacked. A HUD with clickable elements must request it,
	-- and a visible cursor so clicks land.
	Input.SetMode("GameAndUI")
	Input.SetMouseMode("Normal")

	UI.LoadDocument(HUD)
	UI.LoadDocument(POPUP)
	UI.Hide(POPUP)

	-- SetText: bump a counter.
	UI.OnEvent(HUD, "btn_increment", "click", function()
		self.Counter += 1
		UI.SetText(HUD, "counter_value", tostring(self.Counter))
	end)

	-- SetClass: toggle the highlight class on the panel.
	UI.OnEvent(HUD, "btn_highlight", "click", function()
		self.Highlighted = not self.Highlighted
		UI.SetClass(HUD, "panel", "active", self.Highlighted)
	end)

	-- SetInnerRml: inject styled markup into the message element.
	UI.OnEvent(HUD, "btn_rml", "click", function()
		UI.SetInnerRml(HUD, "message",
			"<span style='color: #f38ba8;'>Injected markup at frame " .. tostring(self.Frames) .. "</span>")
	end)

	-- Show / Hide / IsVisible across a second document.
	UI.OnEvent(HUD, "btn_popup", "click", function()
		self.PopupOpen = not self.PopupOpen
		if self.PopupOpen then
			UI.Show(POPUP)
		else
			UI.Hide(POPUP)
		end
		UI.SetText(HUD, "popup_state", UI.IsVisible(POPUP) and "open" or "closed")
	end)

	UI.OnEvent(POPUP, "popup_close", "click", function()
		self.PopupOpen = false
		UI.Hide(POPUP)
		UI.SetText(HUD, "popup_state", "closed")
	end)

	-- Debugger toggle (per-world: attaches to this world's active context).
	UI.OnEvent(HUD, "btn_debugger", "click", function()
		self.DebuggerOn = not self.DebuggerOn
		UI.SetDebuggerVisible(self.DebuggerOn)
	end)

	-- DescribeState: dump the active context + its documents to the console.
	UI.OnEvent(HUD, "btn_describe", "click", function()
		print(UI.DescribeState())
	end)
end

function UITestScript:OnUpdate(DeltaTime: number)
	-- Live readouts, updated every frame via SetText.
	self.Frames += 1
	self.Elapsed += DeltaTime
	UI.SetText(HUD, "frame_value", tostring(self.Frames))
	UI.SetText(HUD, "time_value", string.format("%.1f", self.Elapsed))
end

function UITestScript:OnDetach()
	-- Not strictly required (TeardownWorld frees the whole context), but exercises UnloadDocument.
	UI.UnloadDocument(POPUP)
	UI.UnloadDocument(HUD)
end

return UITestScript
