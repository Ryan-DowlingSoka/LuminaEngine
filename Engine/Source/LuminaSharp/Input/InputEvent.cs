namespace Lumina;

/// <summary>The kind of discrete input event (mirrors the native EInputEventType).</summary>
public enum EInputEventType : byte
{
    KeyDown,      // a keyboard key went down (Repeat set for OS auto-repeat)
    KeyUp,        // a keyboard key was released
    MouseDown,    // a mouse button went down
    MouseUp,      // a mouse button was released
    MouseMove,    // the cursor moved (DeltaX/DeltaY carry the motion)
    MouseScroll,  // the wheel turned (Scroll carries the signed delta)
}

/// <summary>
/// A single discrete input event delivered to <c>EntityScript.OnInput</c> while the entity's
/// <c>SInputComponent</c> is receiving input, react to input as it happens, instead of polling each frame.
/// Key/Mouse Down/Up carry <see cref="KeyCode"/> + modifiers; MouseMove/MouseScroll carry the motion.
/// Letters and digits use their ASCII-upper code as the key value (e.g. 'W' == 87), so
/// <see cref="IsKey"/> is the easy way to test a keyboard key.
/// </summary>
public readonly struct InputEvent
{
    public readonly EInputEventType Type;

    /// <summary>The keyboard key (EKey) or mouse button (EMouseKey) code; 0 for MouseMove/MouseScroll.</summary>
    public readonly int KeyCode;

    /// <summary>True when <see cref="KeyCode"/> is a mouse button (vs a keyboard key).</summary>
    public readonly bool IsMouse;

    public readonly bool Shift;
    public readonly bool Ctrl;
    public readonly bool Alt;

    /// <summary>OS auto-repeat (a held key re-firing KeyDown).</summary>
    public readonly bool Repeat;

    public readonly double MouseX;
    public readonly double MouseY;
    public readonly double DeltaX;
    public readonly double DeltaY;
    public readonly double Scroll;

    internal InputEvent(EInputEventType type, int keyCode, bool isMouse, int mods, bool repeat,
        double mouseX, double mouseY, double deltaX, double deltaY, double scroll)
    {
        Type = type;
        KeyCode = keyCode;
        IsMouse = isMouse;
        Shift = (mods & 1) != 0;
        Ctrl = (mods & 2) != 0;
        Alt = (mods & 4) != 0;
        Repeat = repeat;
        MouseX = mouseX;
        MouseY = mouseY;
        DeltaX = deltaX;
        DeltaY = deltaY;
        Scroll = scroll;
    }

    /// <summary>True if this is a keyboard event whose key is the given letter/digit (case-insensitive).</summary>
    public bool IsKey(char Key)
    {
        return !IsMouse && KeyCode == char.ToUpperInvariant(Key);
    }
}
