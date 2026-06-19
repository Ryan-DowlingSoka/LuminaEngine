using System;
using System.Threading;
using System.Threading.Tasks;

namespace LuminaSharp;

/// <summary>
/// Game-thread <c>await</c> helpers (s&amp;box-style). These complete from game-thread callbacks (the world
/// timer / async asset load), so the continuation resumes on the game thread with the ambient world restored
///, it is safe to touch the world after the await. Pass an <c>EntityScript.DestroyToken</c> so a pending
/// await cancels when the script is destroyed. Do NOT use these from a worker Task body.
/// </summary>
public static class GameTask
{
    /// <summary>Resume after <paramref name="Seconds"/> of world time.</summary>
    public static System.Threading.Tasks.Task DelaySeconds(float Seconds, CancellationToken Token = default)
    {
        TaskCompletionSource Source = new();
        if (!Game.InWorld)
        {
            Source.SetCanceled();
            return Source.Task;
        }
        Lumina.CWorld World = Game.World;
        Entity Self = Game.CurrentEntity;
        World.Timers.Delay(Seconds, () =>
        {
            using (Game.Push(World, Self))
            {
                Source.TrySetResult();
            }
        });
        if (Token.CanBeCanceled)
        {
            Token.Register(() => Source.TrySetCanceled());
        }
        return Source.Task;
    }

    /// <summary>Resume on the next world tick.</summary>
    public static System.Threading.Tasks.Task NextFrame(CancellationToken Token = default) => DelaySeconds(0.0f, Token);

    /// <summary>Load an asset without blocking; resume with the result (or null) on the game thread.</summary>
    public static System.Threading.Tasks.Task<T?> LoadAsync<T>(string Path, CancellationToken Token = default) where T : NativeObject
    {
        TaskCompletionSource<T?> Source = new();
        Lumina.CWorld? World = Game.InWorld ? Game.World : null;
        Entity Self = Game.CurrentEntity;
        Asset.LoadAsync<T>(Path, Result =>
        {
            if (World != null)
            {
                using (Game.Push(World, Self))
                {
                    Source.TrySetResult(Result);
                }
            }
            else
            {
                Source.TrySetResult(Result);
            }
        });
        if (Token.CanBeCanceled)
        {
            Token.Register(() => Source.TrySetCanceled());
        }
        return Source.Task;
    }
}
