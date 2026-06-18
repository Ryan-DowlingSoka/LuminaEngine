using System;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// A world's animation control surface (<c>World.Animation</c>). Two backends share this facade:
///
/// <list type="bullet">
/// <item><b>Single clip</b> (SSimpleAnimationComponent): <see cref="Play"/> / <see cref="Stop"/> /
/// <see cref="Pause"/> a <see cref="CAnimation"/> directly. <see cref="Play"/> adds the component if the
/// entity lacks one.</item>
/// <item><b>Graph</b> (SAnimationGraphComponent): drive a compiled animation graph by setting named
/// float/bool parameters (<see cref="SetFloat"/>, <see cref="SetBool"/>) that gate its state machine.</item>
/// </list>
///
/// Either way the entity needs a skeletal mesh for the pose to render. Every call except <see cref="Play"/>
/// is a safe no-op when the relevant component is absent. Game thread only; forwards to flat
/// <c>LuminaSharp_Animation_*</c> shims (DotNetAnimation.cpp), world Handle injected first.
/// </summary>
public readonly unsafe partial struct Animation
{
    internal readonly ulong Handle;

    internal Animation(ulong Handle)
    {
        this.Handle = Handle;
    }

    public bool IsValid => Handle != 0;

    //~ Single-clip control (SSimpleAnimationComponent).

    /// <summary>Play (or restart) <paramref name="Clip"/> on <paramref name="Target"/>. A null clip stops
    /// playback. <paramref name="Loop"/> false plays once and then reports <see cref="IsFinished"/>.</summary>
    public void Play(Entity Target, CAnimation Clip, bool Loop = false, float Speed = 1.0f)
        => PlayRaw(Target.Id, Clip is null ? IntPtr.Zero : Clip.Handle, Loop ? 1 : 0, Speed);

    /// <summary>Stop playback and snap back to the start (mesh returns to bind pose).</summary>
    public void Stop(Entity Target) => StopRaw(Target.Id);

    /// <summary>Freeze the pose at the current time.</summary>
    public void Pause(Entity Target) => PauseRaw(Target.Id);

    /// <summary>Resume from the current time.</summary>
    public void Resume(Entity Target) => ResumeRaw(Target.Id);

    public bool IsPlaying(Entity Target) => IsPlayingRaw(Target.Id) != 0;

    /// <summary>True once a non-looping clip has run to completion (until the next <see cref="Play"/>).</summary>
    public bool IsFinished(Entity Target) => IsFinishedRaw(Target.Id) != 0;

    public void SetSpeed(Entity Target, float Speed) => SetSpeedRaw(Target.Id, Speed);

    /// <summary>Scrub the playhead to <paramref name="Time"/> seconds.</summary>
    public void SetTime(Entity Target, float Time) => SetTimeRaw(Target.Id, Time);

    public float GetTime(Entity Target) => GetTimeRaw(Target.Id);

    //~ Graph parameters (SAnimationGraphComponent). No-op when the entity has no graph component or no such parameter.

    public void SetFloat(Entity Target, string Name, float Value) => SetFloatRaw(Target.Id, Name, Value);
    public float GetFloat(Entity Target, string Name, float Default = 0.0f) => GetFloatRaw(Target.Id, Name, Default);
    public void SetBool(Entity Target, string Name, bool Value) => SetBoolRaw(Target.Id, Name, Value ? 1 : 0);
    public bool GetBool(Entity Target, string Name, bool Default = false) => GetBoolRaw(Target.Id, Name, Default ? 1 : 0) != 0;
    public bool HasParameter(Entity Target, string Name) => HasParameterRaw(Target.Id, Name) != 0;

    // Flat shims (Runtime module). World Handle injected first; CAnimation crosses as its native pointer.
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_Play")]
    private partial void PlayRaw(uint Entity, IntPtr Clip, int Loop, float Speed);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_Stop")]
    private partial void StopRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_Pause")]
    private partial void PauseRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_Resume")]
    private partial void ResumeRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_IsPlaying")]
    private partial int IsPlayingRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_IsFinished")]
    private partial int IsFinishedRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_SetSpeed")]
    private partial void SetSpeedRaw(uint Entity, float Speed);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_SetTime")]
    private partial void SetTimeRaw(uint Entity, float Time);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_GetTime")]
    private partial float GetTimeRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_SetFloat")]
    private partial void SetFloatRaw(uint Entity, string Name, float Value);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_GetFloat")]
    private partial float GetFloatRaw(uint Entity, string Name, float Default);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_SetBool")]
    private partial void SetBoolRaw(uint Entity, string Name, int Value);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_GetBool")]
    private partial int GetBoolRaw(uint Entity, string Name, int Default);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Animation_HasParameter")]
    private partial int HasParameterRaw(uint Entity, string Name);
}
