using Lumina;

namespace LuminaSharp;

/// <summary>Play sounds in the current world (s&amp;box-style <c>Sound.Play</c>). Wraps <see cref="Game.World"/>'s
/// audio facade and returns a <see cref="PlayingSound"/> you can adjust or stop.</summary>
public static class Sound
{
    public static PlayingSound Play(CAudioStream Clip, float Volume = 1.0f, float Pitch = 1.0f, bool Loop = false)
        => new(Game.World, Game.World.Audio.Play2D(Clip, Volume, Pitch, Loop));

    public static PlayingSound PlayAt(CAudioStream Clip, FVector3 Location, float Volume = 1.0f, float Pitch = 1.0f,
        float MinDistance = 1.0f, float MaxDistance = 50.0f, bool Loop = false)
        => new(Game.World, Game.World.Audio.PlayAtLocation(Clip, Location, Volume, Pitch, MinDistance, MaxDistance, Loop));

    public static void StopAll() => Game.World.Audio.StopAll();
}

/// <summary>A live sound returned by <see cref="Sound.Play"/>. Carries its world, so the setters work even
/// after the originating callback returns.</summary>
public readonly struct PlayingSound
{
    private readonly CWorld World;
    public readonly AudioHandle Handle;

    internal PlayingSound(CWorld World, AudioHandle Handle)
    {
        this.World = World;
        this.Handle = Handle;
    }

    public bool IsValid => Handle.IsValid;
    public float Volume { set => World.Audio.SetVolume(Handle, value); }
    public float Pitch { set => World.Audio.SetPitch(Handle, value); }
    public FVector3 Position { set => World.Audio.SetPosition(Handle, value); }
    public bool Looping { set => World.Audio.SetLooping(Handle, value); }

    public void Stop(bool FadeOut = false) => World.Audio.Stop(Handle, FadeOut);
}
