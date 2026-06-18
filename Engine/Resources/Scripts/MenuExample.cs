using System;
using LuminaSharp;
using Lumina;

namespace Engine;

/// <summary>
/// End-to-end example of the World.UI scripting surface: loads a screen-space RmlUi document, shows it,
/// wires button clicks back to C#, and mutates the live DOM in response. Attach to any entity in a game
/// world (add a C# Script component pointing at this script) and press Play.
/// </summary>
public sealed class MenuExample : EntityScript
{
    [Property(Tooltip = "RML document shown on screen.", AssetType = "rml")]
    public string Document = "/Engine/Resources/Content/UI/Examples/Menu.rml";

    private UIDocument _Menu;
    private UIEventSubscription? _PlayClick;
    private UIEventSubscription? _ToggleClick;

    private int _PlayCount;
    private bool _DarkTheme = true;

    public override void OnReady()
    {
        _Menu = World.UI.LoadDocument(Document);
        if (!_Menu.IsValid)
        {
            Debug.LogError($"MenuExample: failed to load UI document '{Document}'.");
            return;
        }

        // Wire callbacks BEFORE showing so the very first click is handled.
        _PlayClick   = _Menu["play"].OnClick(OnPlay);
        _ToggleClick = _Menu["toggle"].OnClick(OnToggleTheme);

        _Menu.Show();

        // Free the cursor so the buttons are clickable; gameplay still receives the rest of the input.
        World.UI.EnableCursor();

        Debug.Log("MenuExample: menu shown. Click 'Play' or 'Toggle Theme'.");
    }

    private void OnPlay(UIEvent Event)
    {
        _PlayCount++;

        // Chained calls use the method form (SetText): you can't assign a struct property on an rvalue.
        _Menu["status"].SetText($"Play clicked {_PlayCount}x");

        // Example of reading the event + driving gameplay from a UI callback.
        if (_PlayCount >= 3)
        {
            _Menu["status"].SetText("Starting...");
            // World.SpawnPrefab(...), travel to a level, etc.
        }
    }

    private void OnToggleTheme(UIEvent Event)
    {
        _DarkTheme = !_DarkTheme;

        // A stored element variable supports the property setters too (e.Text = ..., e.Visible = ...).
        _Menu["panel"].SetStyle("background-color", _DarkTheme ? "#1e1e2eff" : "#eff1f5ff");
        _Menu.Root.SetStyle("color", _DarkTheme ? "#e0e0e0" : "#1e1e2e");
        _Menu["status"].SetText(_DarkTheme ? "Dark theme" : "Light theme");
    }

    public override void OnDetach()
    {
        // Release listeners before the world (and its UI context) tears down.
        _PlayClick?.Dispose();
        _ToggleClick?.Dispose();
        _Menu.Close();
        World.UI.DisableCursor();
    }
}
