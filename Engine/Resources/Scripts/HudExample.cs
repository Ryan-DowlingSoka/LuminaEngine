using System;
using LuminaSharp;
using Lumina;

namespace Engine;

/// <summary>
/// MVVM test for the composed HUD (Composition/HudComposed.rml): one <see cref="ViewModel"/> drives the
/// clock, health bar and minimap blip, three separate <c>&lt;template&gt;</c> widgets that all bind to the
/// document's "hud" model. The script only animates view-model properties; the composed widgets update
/// themselves. Attach to any entity in a game world and press Play.
/// </summary>
public sealed class HudExample : EntityScript
{
    [Property(Tooltip = "Composed HUD document.", AssetType = "rml")]
    public string Document = "/Engine/Resources/Content/UI/Examples/Composition/HudComposed.rml";

    private sealed class HudModel : ViewModel
    {
        private string _Time = "00:00";
        private int _Health = 100;
        private int _BlipX = 84;
        private int _BlipY = 50;

        /// <summary>Clock readout ({{ Time }} in Clock.rml).</summary>
        [Bind] public string Time { get => _Time; set => Set(ref _Time, value); }

        /// <summary>0..100; drives the health bar fill width and its label.</summary>
        [Bind] public int Health { get => _Health; set => Set(ref _Health, value); }

        /// <summary>Minimap blip position, percent of the map (0..100).</summary>
        [Bind] public int BlipX { get => _BlipX; set => Set(ref _BlipX, value); }
        [Bind] public int BlipY { get => _BlipY; set => Set(ref _BlipY, value); }
    }

    private HudModel _Model = null!;
    private UIDataModel _Binding = null!;
    private UIDocument _Hud;

    public override void OnReady()
    {
        _Model = new HudModel();

        // Register the model BEFORE loading the document that binds to it.
        _Binding = World.UI.AddModel("hud", _Model);
        if (!_Binding.IsValid)
        {
            Debug.LogError("HudExample: failed to register the 'hud' data model.");
            return;
        }

        _Hud = World.UI.LoadDocument(Document);
        if (!_Hud.IsValid)
        {
            Debug.LogError($"HudExample: failed to load HUD document '{Document}'.");
            return;
        }

        _Hud.Show();
        Debug.Log("HudExample: HUD shown (MVVM). Clock, health and minimap update live from one model.");
    }

    public override void OnUpdate(float DeltaTime)
    {
        if (!_Model.IsBound)
        {
            return;
        }

        float T = (float)Time.Now;

        // Running mm:ss clock from the world time.
        int Seconds = (int)T;
        _Model.Time = $"{(Seconds / 60) % 60:00}:{Seconds % 60:00}";

        // Health eases between 20 and 100 on a slow cycle.
        _Model.Health = (int)Mathf.Lerp(20.0f, 100.0f, 0.5f * (1.0f + MathF.Sin(T * 0.6f)));

        // Blip orbits the minimap center (50,50) at radius 34%.
        _Model.BlipX = (int)(50.0f + 34.0f * MathF.Cos(T * 1.2f));
        _Model.BlipY = (int)(50.0f + 34.0f * MathF.Sin(T * 1.2f));
    }

    public override void OnDetach()
    {
        _Hud.Close();
        _Binding?.Dispose();
    }
}
