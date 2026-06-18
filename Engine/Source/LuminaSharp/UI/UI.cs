using System;

namespace LuminaSharp;

/// <summary>How a world's viewport routes raw input. Mirrors the engine <c>EInputMode</c>.</summary>
public enum UIInputMode
{
    /// <summary>Input drives gameplay only; the UI does not receive mouse/keyboard.</summary>
    Game = 0,
    /// <summary>Input drives the UI only; gameplay input is gated off.</summary>
    UI = 1,
    /// <summary>Input drives both: the UI gets first refusal, the rest reaches gameplay.</summary>
    GameAndUI = 2,
}

/// <summary>Cursor visibility/capture for a world's viewport. Mirrors the engine <c>EMouseMode</c>.</summary>
public enum UICursorMode
{
    /// <summary>Cursor hidden but free to move.</summary>
    Hidden = 0,
    /// <summary>Cursor visible and free (use this for menus / pointer UI).</summary>
    Normal = 1,
    /// <summary>Cursor hidden and locked to the window (use this for mouselook gameplay).</summary>
    Captured = 2,
}

/// <summary>
/// A world's UI interface (<c>World.UI</c>): load and present screen-space RmlUi documents and route the
/// cursor between gameplay and the UI. The C# mirror of the other gameplay facades; backed by the
/// <c>LuminaSharp_UI_*</c> exports. Game thread only. Documents render full-screen over this world's view
/// (the same context mouse/keyboard input is already forwarded to), distinct from world-space
/// <c>SWidgetComponent</c> billboards.
/// </summary>
public readonly unsafe partial struct UI
{
    internal readonly ulong Handle; // CWorld*

    internal UI(ulong Handle)
    {
        this.Handle = Handle;
    }

    public bool IsValid => Handle != 0;

    /// <summary>
    /// Loads the RML document at <paramref name="Path"/> (a virtual path, e.g.
    /// "/Game/UI/HUD.rml") into this world's screen context. The document starts hidden -- call
    /// <see cref="UIDocument.Show"/>. Returns an invalid <see cref="UIDocument"/> on load/parse failure.
    /// </summary>
    public UIDocument LoadDocument(string Path) => new(Handle, Native.UI_LoadDocument(Handle, Path));

    /// <summary>Loads a document from an in-memory RML string. <paramref name="SourceUrl"/> resolves relative
    /// includes (stylesheets, images). Useful for procedurally built UI.</summary>
    public UIDocument LoadDocumentFromMemory(string Rml, string SourceUrl = "[inline]")
        => new(Handle, Native.UI_LoadDocumentFromMemory(Handle, Rml, SourceUrl));

    /// <summary>Sets how this world's viewport routes input.</summary>
    public void SetInputMode(UIInputMode Mode) => Native.UI_SetInputMode(Handle, (int)Mode);

    /// <summary>Sets this world's cursor visibility/capture.</summary>
    public void SetCursorMode(UICursorMode Mode) => Native.UI_SetMouseMode(Handle, (int)Mode);

    /// <summary>Show a free cursor and let the UI receive clicks while gameplay still gets the rest
    /// (<see cref="UIInputMode.GameAndUI"/> + <see cref="UICursorMode.Normal"/>). Call when a menu opens.</summary>
    public void EnableCursor()
    {
        SetInputMode(UIInputMode.GameAndUI);
        SetCursorMode(UICursorMode.Normal);
    }

    /// <summary>Hide + capture the cursor for mouselook and route input back to gameplay
    /// (<see cref="UIInputMode.Game"/> + <see cref="UICursorMode.Captured"/>). Call when a menu closes.</summary>
    public void DisableCursor()
    {
        SetInputMode(UIInputMode.Game);
        SetCursorMode(UICursorMode.Captured);
    }
}
