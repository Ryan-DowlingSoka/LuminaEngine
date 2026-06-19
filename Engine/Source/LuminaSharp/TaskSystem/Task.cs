using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// C# mirror of the engine's native <c>Lumina::Task</c> system: parallel-for over a worker pool, one-shot
/// async work, and waiting. Forwards to flat <c>LuminaSharp_Task_*</c> shims in the Runtime module
/// (DotNetTask.cpp) via the [NativeCall] generator.
///
/// FIBER / CLR CAVEAT: task bodies run SYNCHRONOUSLY on the engine's Win32-fiber worker threads. A body
/// MUST be self-contained compute: it must NOT block, await, yield, take a long-held native lock, or
/// recursively call back into the Task system. A managed body that yields its fiber would break CLR
/// thread-affinity and corrupt the runtime. Parallel compute (the supported use) only.
/// </summary>
public static unsafe partial class Task
{
    // Maps to Lumina::ETaskPriority (High=0, Medium=1, Low=2). Bodies use Medium.
    private const int PriorityMedium = 1;

    /// <summary>
    /// Splits <paramref name="Count"/> indices across worker threads and runs <paramref name="Body"/> for
    /// each. BLOCKS until every index has been processed. The body runs on worker fibers (see the class
    /// caveat). The delegate is kept alive for the whole call via a pinned GCHandle, freed when it returns.
    /// </summary>
    public static void ParallelFor(int Count, Action<int> Body)
    {
        if (Count <= 0)
        {
            return;
        }

        // ParallelFor blocks, so the GCHandle is valid for the whole native call; free it in finally.
        GCHandle Gc = GCHandle.Alloc(Body);
        try
        {
            NativeParallelFor(
                (uint)Count,
                0,
                (IntPtr)(delegate* unmanaged[Cdecl]<void*, uint, uint, uint, void>)&ParallelThunk,
                GCHandle.ToIntPtr(Gc),
                PriorityMedium);
        }
        finally
        {
            Gc.Free();
        }
    }

    // Native -> managed entry for ParallelFor chunks. Resolves the Action<int> from the GCHandle Ctx and
    // runs it over [Start, End). Never throws across the boundary: funnels to Interop.LogException.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void ParallelThunk(void* Ctx, uint Start, uint End, uint Thread)
    {
        try
        {
            Action<int>? Body = GCHandle.FromIntPtr((IntPtr)Ctx).Target as Action<int>;
            if (Body == null)
            {
                return;
            }

            for (uint i = Start; i < End; i++)
            {
                Body((int)i);
            }
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    /// <summary>
    /// Schedules <paramref name="Body"/> to run ONCE on a worker thread and returns a handle to wait on.
    /// The body runs on a worker fiber (see the class caveat). The returned <see cref="TaskHandle"/> owns a
    /// native completion handle: you MUST <c>Wait()</c> then <c>Dispose()</c> (or just <c>Dispose()</c>) it,
    /// otherwise the native handle leaks. The body's GCHandle is freed by the thunk once the body has run.
    /// </summary>
    public static TaskHandle Run(Action Body)
    {
        GCHandle Gc = GCHandle.Alloc(Body);
        IntPtr Handle = NativeRun(
            (IntPtr)(delegate* unmanaged[Cdecl]<void*, uint, uint, uint, void>)&RunThunk,
            GCHandle.ToIntPtr(Gc),
            PriorityMedium);
        return new TaskHandle(Handle);
    }

    // Native -> managed entry for Run. Resolves and invokes the Action once, then frees its GCHandle (the
    // body has run, so the handle is no longer needed). Never throws across the boundary.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void RunThunk(void* Ctx, uint Start, uint End, uint Thread)
    {
        GCHandle Gc = GCHandle.FromIntPtr((IntPtr)Ctx);
        try
        {
            Action? Body = Gc.Target as Action;
            Body?.Invoke();
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
        finally
        {
            Gc.Free();
        }
    }

    /// <summary>Blocks until every task submitted so far has completed.</summary>
    public static void WaitForAll()
    {
        NativeWaitForAll();
    }

    /// <summary>Number of background worker threads in the engine task system.</summary>
    public static int WorkerCount => NativeNumWorkers();

    //~ Native binds. Resolved from the Runtime module by the [NativeCall] generator. IntPtr/uint/int pass
    //  through; void returns nothing.

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Task_ParallelFor")]
    private static partial void NativeParallelFor(uint Num, uint MinRange, IntPtr Thunk, IntPtr Ctx, int Priority);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Task_Run")]
    private static partial IntPtr NativeRun(IntPtr Thunk, IntPtr Ctx, int Priority);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Task_Wait")]
    internal static partial void NativeWait(IntPtr Handle);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Task_Release")]
    internal static partial void NativeRelease(IntPtr Handle);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Task_WaitForAll")]
    private static partial void NativeWaitForAll();

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Task_NumWorkers")]
    private static partial int NativeNumWorkers();
}

/// <summary>
/// A handle to a single async task scheduled by <see cref="Task.Run"/>. Owns a native completion handle;
/// you MUST <see cref="Dispose"/> it exactly once (directly, or after <see cref="Wait"/>) to release the
/// native handle. <see cref="Wait"/> may be called repeatedly (waiting a completed task is a no-op);
/// <see cref="Dispose"/> must NOT be called twice (this is a value type, so it cannot guard itself - a
/// second Dispose double-frees). Use a <c>using</c> block, or Wait() then Dispose() once.
/// </summary>
public readonly struct TaskHandle : IDisposable
{
    internal readonly IntPtr Handle;

    internal TaskHandle(IntPtr Handle)
    {
        this.Handle = Handle;
    }

    /// <summary>True if this handle refers to a real scheduled task.</summary>
    public bool IsValid => Handle != IntPtr.Zero;

    /// <summary>Blocks until the task has completed.</summary>
    public void Wait()
    {
        if (Handle != IntPtr.Zero)
        {
            Task.NativeWait(Handle);
        }
    }

    /// <summary>Releases the native completion handle. Call exactly once per <see cref="Task.Run"/>.</summary>
    public void Dispose()
    {
        if (Handle != IntPtr.Zero)
        {
            Task.NativeRelease(Handle);
        }
    }
}
