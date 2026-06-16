namespace LuminaSharp;

/// <summary>
/// Global logging into the engine log, usable from anywhere (the C# analog of Unity's Debug). The
/// first of the global engine API classes; Time/Input/Physics/etc. follow the same pattern.
/// </summary>
public static class Debug
{
    public static void Log(string Message)
    {
        Native.Log(ELogLevel.Info, Message);
    }

    public static void LogWarning(string Message)
    {
        Native.Log(ELogLevel.Warn, Message);
    }

    public static void LogError(string Message)
    {
        Native.Log(ELogLevel.Error, Message);
    }
}
