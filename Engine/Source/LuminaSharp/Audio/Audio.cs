using System;
using System.Runtime.InteropServices;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// Opaque handle to a playing sound (the blittable mirror of the engine's FAudioHandle). Returned by the
/// <c>World.Audio.Play*</c> calls and passed back to control or stop that voice. A default/invalid handle
/// is safe to pass to every control call (they no-op).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct AudioHandle
{
    public readonly uint Generation;
    public readonly uint Index;

    /// <summary>False for a sound that failed to start (no asset data / audio disabled).</summary>
    public bool IsValid => Generation != 0;
}

/// <summary>
/// A world's audio interface (<c>World.Audio</c>). Plays <see cref="CAudioStream"/> assets as 2D (UI/music)
/// or spatialized 3D sounds and controls them through the returned <see cref="AudioHandle"/>. The engine
/// audio context is process-global, so the World handle is currently unused; the facade hangs off the world
/// for discoverability. Game thread only; every member forwards to a flat <c>LuminaSharp_Audio_*</c> shim
/// (DotNetAudio.cpp). Each call queues a command for the audio pump, so a stale handle simply no-ops.
/// </summary>
public readonly unsafe partial struct Audio
{
    internal readonly ulong Handle;

    internal Audio(ulong Handle)
    {
        this.Handle = Handle;
    }

    /// <summary>Play a non-spatialized sound (UI, music, 2D SFX). Returns the controlling handle.</summary>
    public AudioHandle Play2D(CAudioStream Sound, float Volume = 1.0f, float Pitch = 1.0f, bool Loop = false)
        => Sound is null ? default : PlaySound2DRaw(Sound.Handle, Volume, Pitch, Loop ? 1 : 0);

    /// <summary>Play a spatialized sound at a world location, attenuated between Min and Max distance (meters).</summary>
    public AudioHandle PlayAtLocation(CAudioStream Sound, FVector3 Location, float Volume = 1.0f, float Pitch = 1.0f,
        float MinDistance = 1.0f, float MaxDistance = 50.0f, bool Loop = false)
        => Sound is null ? default : PlaySoundAtLocationRaw(Sound.Handle, Location, Volume, Pitch, MinDistance, MaxDistance, Loop ? 1 : 0);

    /// <summary>Stop a playing sound. <paramref name="FadeOut"/> allows the sound's fade-out curve rather than cutting instantly.</summary>
    public void Stop(AudioHandle Handle, bool FadeOut = false) => StopRaw(Handle, FadeOut ? 1 : 0);

    /// <summary>Stop every playing sound.</summary>
    public void StopAll() => StopAllRaw();

    public void SetVolume(AudioHandle Handle, float Volume) => SetVolumeRaw(Handle, Volume);
    public void SetPitch(AudioHandle Handle, float Pitch) => SetPitchRaw(Handle, Pitch);
    public void SetLooping(AudioHandle Handle, bool Loop) => SetLoopingRaw(Handle, Loop ? 1 : 0);

    /// <summary>Move a spatialized sound to a new world position (e.g. tracking a moving emitter).</summary>
    public void SetPosition(AudioHandle Handle, FVector3 Position) => SetPositionRaw(Handle, Position);
    public void SetMinMaxDistance(AudioHandle Handle, float MinDistance, float MaxDistance) => SetMinMaxDistanceRaw(Handle, MinDistance, MaxDistance);

    // Flat shims (Runtime module). The world Handle is injected first; the CAudioStream crosses as its native pointer.
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_PlaySound2D")]
    private partial AudioHandle PlaySound2DRaw(IntPtr Stream, float Volume, float Pitch, int Loop);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_PlaySoundAtLocation")]
    private partial AudioHandle PlaySoundAtLocationRaw(IntPtr Stream, FVector3 Location, float Volume, float Pitch, float MinDistance, float MaxDistance, int Loop);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_Stop")]
    private partial void StopRaw(AudioHandle Voice, int AllowFadeOut);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_StopAll")]
    private partial void StopAllRaw();

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_SetVolume")]
    private partial void SetVolumeRaw(AudioHandle Voice, float Volume);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_SetPitch")]
    private partial void SetPitchRaw(AudioHandle Voice, float Pitch);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_SetLooping")]
    private partial void SetLoopingRaw(AudioHandle Voice, int Loop);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_SetPosition")]
    private partial void SetPositionRaw(AudioHandle Voice, FVector3 Position);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Audio_SetMinMaxDistance")]
    private partial void SetMinMaxDistanceRaw(AudioHandle Voice, float MinDistance, float MaxDistance);
}
