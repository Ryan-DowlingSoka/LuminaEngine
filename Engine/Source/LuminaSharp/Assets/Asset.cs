using System;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// Loads engine assets from script, the C# analog of Lua's Asset.* library and C++ StaticLoadObject /
/// FSoftObjectPath. Paths are virtual asset paths (e.g. "/Game/Materials/Brick"). A returned object is a
/// thin wrapper over the native CObject; assign it to a component property
/// (<c>mesh.StaticMesh = Asset.Load&lt;CStaticMesh&gt;(path)</c>) to keep it alive through the engine's refcount.
/// </summary>
public static class Asset
{
    /// <summary>Synchronously (blocking) loads the asset at <paramref name="Path"/> as T, or null on failure.</summary>
    public static T? Load<T>(string Path) where T : NativeObject
    {
        IntPtr Pointer = Native.LoadObject(Path);
        if (Pointer == IntPtr.Zero)
        {
            return null;
        }
        return Wrapper<T>.Create(Pointer);
    }

    /// <summary>True if an asset exists at <paramref name="Path"/> in the registry (a probe, no load).</summary>
    public static bool Exists(string Path)
    {
        return Native.AssetExists(Path);
    }

    /// <summary>
    /// Asynchronously loads the asset at <paramref name="Path"/>; <paramref name="Callback"/> runs on the
    /// game thread with the loaded T (or null), exactly once.
    /// </summary>
    public static void LoadAsync<T>(string Path, Action<T?> Callback) where T : NativeObject
    {
        // Capture T in a trampoline so the requested type survives the type-erased native round-trip: the
        // native side hands back a GCHandle to this Action, which Host.InvokeAssetCallback resolves + frees.
        Action<IntPtr> Trampoline = Pointer =>
        {
            Callback(Pointer == IntPtr.Zero ? null : Wrapper<T>.Create(Pointer));
        };
        GCHandle Handle = GCHandle.Alloc(Trampoline);
        Native.LoadObjectAsync(Path, GCHandle.ToIntPtr(Handle));
    }
}
