using System;
using LuminaSharp;
using Lumina;

namespace Engine;

/// <summary>
/// End-to-end example of the World.UI MVVM surface: a <see cref="ViewModel"/> drives a screen-space RmlUi
/// document through data bindings instead of poking the DOM. The RML declares what to show
/// (<c>{{ Status }}</c>, <c>data-event-click="Play()"</c>, <c>data-class-light="!Dark"</c>); the script only
/// changes view-model properties and the view updates itself. Attach to any entity in a game world (add a C#
/// Script component pointing at this script) and press Play. See Menu.rml / Menu.rcss alongside.
/// </summary>
public sealed class MenuExample : EntityScript
{
    [Property(Tooltip = "RML document shown on screen.", AssetType = "rml")]
    public string Document = "/Engine/Resources/Content/UI/Examples/Menu.rml";

    /// <summary>The view-model behind Menu.rml. Bound properties flow to the view; commands flow back from it.</summary>
    private sealed class MenuModel : ViewModel
    {
        private string _Status = "Click Play to begin.";
        private bool _Dark = true;
        private int _PlayCount;

        /// <summary>Status line text, shown via <c>{{ Status }}</c>.</summary>
        [Bind] public string Status { get => _Status; set => Set(ref _Status, value); }

        /// <summary>Dark theme on/off; drives <c>data-class-light="!Dark"</c> on the panel.</summary>
        [Bind] public bool Dark { get => _Dark; set => Set(ref _Dark, value); }

        /// <summary>The toggle button's label = the NEXT action. Computed; re-pushed when <see cref="Dark"/> flips.</summary>
        [Bind] public string ThemeLabel => _Dark ? "Light theme" : "Dark theme";

        // Commands invoked from RML via data-event-click. No element lookups, no listener wiring.
        [BindCommand]
        public void Play()
        {
            _PlayCount++;
            Status = _PlayCount >= 3 ? "Starting..." : $"Play clicked {_PlayCount}x";
        }

        [BindCommand]
        public void ToggleTheme()
        {
            Dark = !_Dark;                       // pushes Dark -> the panel restyles via data-class-light
            NotifyChanged(nameof(ThemeLabel));   // computed label depends on Dark
            Status = _Dark ? "Dark theme" : "Light theme";
        }
    }

    private MenuModel _Model = null!;
    private UIDataModel _Binding = null!;
    private UIDocument _Menu;

    public override void OnReady()
    {
        _Model = new MenuModel();

        // Register the data model BEFORE loading the document -- RmlUi resolves data bindings at load time.
        _Binding = World.UI.AddModel("menu", _Model);
        if (!_Binding.IsValid)
        {
            Debug.LogError("MenuExample: failed to register the 'menu' data model.");
            return;
        }

        _Menu = World.UI.LoadDocument(Document);
        if (!_Menu.IsValid)
        {
            Debug.LogError($"MenuExample: failed to load UI document '{Document}'.");
            return;
        }

        _Menu.Show();

        // Free the cursor so the buttons are clickable; gameplay still receives the rest of the input.
        World.UI.EnableCursor();

        Debug.Log("MenuExample: menu shown (MVVM). Click 'Play' or 'Toggle Theme'.");
    }

    public override void OnDetach()
    {
        // Tear down in reverse: close the document, then drop the model (frees the managed callback).
        _Menu.Close();
        _Binding?.Dispose();
        World.UI.DisableCursor();
    }
}
