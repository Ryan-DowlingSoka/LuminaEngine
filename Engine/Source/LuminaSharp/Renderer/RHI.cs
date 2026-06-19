using System;
using Lumina;

namespace LuminaSharp.Rendering;

/// <summary>
/// Direct 1:1 binding to the engine Render Hardware Interface (C++ <c>Lumina::RHI</c>). Every call is a thin
/// <c>delegate*</c> into the native RHI free function, handles and descriptors are blittable and cross by
/// value with zero marshalling, so this is as fast as the C++ API. The hot per-call <c>Cmd*</c> recorders
/// (see RHICommands.cs) additionally skip the GC transition.
///
/// Lifetime/threading match the native RHI exactly: command lists are per-thread, recording is lock-free,
/// and resource creation/submission are the synchronized operations. Free resources with <see cref="FreeH"/>
/// (or the <c>Free</c> calls). The GPU device and the per-frame loop are owned by the engine: there is no
/// device create/destroy on this surface, and a script must not drive frame pacing itself.
/// </summary>
public static unsafe partial class RHI
{
    // ---- Device ----
    // The GPU device, the per-frame loop, and the window swapchain are all owned by the engine.
    // Scripts cannot create, destroy, tick, present, or stall the device. To wait on work they
    // submitted, they signal a timeline semaphore on Submit and WaitSemaphore on it.

    [NativeCall("LuminaSharp_RHI_WaitSemaphore")]
    public static partial void WaitSemaphore(FSemaphoreH Semaphore, ulong Value);

    // ---- Memory ----

    /// <summary>
    /// Allocate device memory. <paramref name="Alignment"/> defaults to 16 bytes and <paramref name="Type"/>
    /// to <see cref="EMemoryType.CPUWrite"/> (host-visible, CPU read and write), so <c>Malloc(Size)</c> is
    /// the common case.
    /// </summary>
    [NativeCall("LuminaSharp_RHI_Malloc")]
    public static partial GPUPtr Malloc(ulong Size, ulong Alignment = 16, EMemoryType Type = EMemoryType.Default);

    /// <summary>Allocate device memory of the given type at default alignment (mirrors the C++ overload).</summary>
    public static GPUPtr Malloc(ulong Size, EMemoryType Type) => Malloc(Size, 16, Type);

    /// <summary>Map a GPU pointer to a CPU-visible address (valid for CPU-visible memory types).</summary>
    [NativeCall("LuminaSharp_RHI_ToHost", SuppressGCTransition = true)]
    public static partial IntPtr ToHost(GPUPtr Gpu);

    [NativeCall("LuminaSharp_RHI_Free")]
    public static partial void Free(GPUPtr Gpu);

    // The generator keys its delegate field on the method name, so the typed free thunks have unique names
    // and the ergonomic overloaded FreeH(...) forwards to them.
    [NativeCall("LuminaSharp_RHI_FreeSemaphore")] private static partial void FreeSemaphore(FSemaphoreH H);
    [NativeCall("LuminaSharp_RHI_FreePipeline")] private static partial void FreePipeline(FPipelineH H);
    [NativeCall("LuminaSharp_RHI_FreeTexture")] private static partial void FreeTexture(FTextureH H);
    [NativeCall("LuminaSharp_RHI_FreeTextureHeap")] private static partial void FreeTextureHeap(FTextureHeapH H);
    [NativeCall("LuminaSharp_RHI_FreeDepthStencil")] private static partial void FreeDepthStencil(FDepthStencilH H);

    public static void FreeH(FSemaphoreH Semaphore) => FreeSemaphore(Semaphore);
    public static void FreeH(FPipelineH Pipeline) => FreePipeline(Pipeline);
    public static void FreeH(FTextureH Texture) => FreeTexture(Texture);
    public static void FreeH(FTextureHeapH Heap) => FreeTextureHeap(Heap);
    public static void FreeH(FDepthStencilH DepthStencil) => FreeDepthStencil(DepthStencil);

    // ---- Resources ----

    [NativeCall("LuminaSharp_RHI_CreateDepthStencil")]
    public static partial FDepthStencilH CreateDepthStencil(FDepthStencilDesc Desc);

    [NativeCall("LuminaSharp_RHI_CreateSemaphore")]
    public static partial FSemaphoreH CreateSemaphore(ulong Value);

    /// <summary>Create a texture. Omit <paramref name="Location"/> (the default) for engine-managed backing memory.</summary>
    [NativeCall("LuminaSharp_RHI_CreateTexture")]
    public static partial FTextureH CreateTexture(FTextureDesc Desc, GPUPtr Location = default);

    [NativeCall("LuminaSharp_RHI_CreateTextureHeap")]
    public static partial FTextureHeapH CreateTextureHeap(uint TextureCount, uint RWTextureCount, uint SamplerCount);

    [NativeCall("LuminaSharp_RHI_GetTextureDesc", SuppressGCTransition = true)]
    public static partial FTextureDesc GetTextureDesc(FTextureH Texture);

    /// <summary>
    /// Create a graphics pipeline from compiled shader bytecode. <paramref name="ColorTargets"/> and
    /// <paramref name="Constants"/> may be empty. See <see cref="Core.CreateGraphicsPipeline"/> to build one
    /// from named shaders in the engine shader library instead (RHICore.CreateGraphicsPipeline).
    /// </summary>
    [NativeCall("LuminaSharp_RHI_CreateGraphicsPipeline")]
    public static partial FPipelineH CreateGraphicsPipeline(
        ReadOnlySpan<byte> VertexCode, string VertexEntry,
        ReadOnlySpan<byte> FragmentCode, string FragmentEntry,
        FRasterDesc Raster, ReadOnlySpan<FColorTarget> ColorTargets,
        ReadOnlySpan<FSpecializationConstant> Constants);

    [NativeCall("LuminaSharp_RHI_CreateComputePipeline")]
    public static partial FPipelineH CreateComputePipeline(
        ReadOnlySpan<byte> ComputeCode, string ComputeEntry, ReadOnlySpan<FSpecializationConstant> Constants);

    // ---- Texture heap ----

    [NativeCall("LuminaSharp_RHI_HeapWriteTexture")]
    public static partial uint HeapWriteTexture(FTextureHeapH Heap, FTextureH Texture);

    /// <summary>Write a storage-image slot. <paramref name="Mip"/> defaults to 0.</summary>
    [NativeCall("LuminaSharp_RHI_HeapWriteRWTexture")]
    public static partial uint HeapWriteRWTexture(FTextureHeapH Heap, FTextureH Texture, uint Mip = 0);

    [NativeCall("LuminaSharp_RHI_HeapWriteSampler")]
    public static partial uint HeapWriteSampler(FTextureHeapH Heap, FSamplerDesc Desc);

    [NativeCall("LuminaSharp_RHI_HeapFreeTexture")]
    public static partial void HeapFreeTexture(FTextureHeapH Heap, uint Slot);
    [NativeCall("LuminaSharp_RHI_HeapFreeRWTexture")]
    public static partial void HeapFreeRWTexture(FTextureHeapH Heap, uint Slot);
    [NativeCall("LuminaSharp_RHI_HeapFreeSampler")]
    public static partial void HeapFreeSampler(FTextureHeapH Heap, uint Slot);

    [NativeCall("LuminaSharp_RHI_HeapTextureCount")]
    public static partial int HeapTextureCount(FTextureHeapH Heap);

    [NativeCall("LuminaSharp_RHI_HeapTextureAt")]
    public static partial FHeapTextureInfo HeapTextureAt(FTextureHeapH Heap, int Index);

    /// <summary>Debug: every occupied sampled slot in the heap.</summary>
    public static FHeapTextureInfo[] GetTextureHeapTextures(FTextureHeapH Heap)
    {
        int Count = HeapTextureCount(Heap);
        if (Count <= 0)
        {
            return Array.Empty<FHeapTextureInfo>();
        }
        FHeapTextureInfo[] Result = new FHeapTextureInfo[Count];
        for (int i = 0; i < Count; i++)
        {
            Result[i] = HeapTextureAt(Heap, i);
        }
        return Result;
    }

    // ---- Command lists / submission ----

    /// <summary>Open a command list on the given queue. <paramref name="Type"/> defaults to the graphics queue.</summary>
    [NativeCall("LuminaSharp_RHI_OpenCommandList")]
    public static partial FCmdListH OpenCommandList(EQueueType Type = EQueueType.Default);

    [NativeCall("LuminaSharp_RHI_ResetCommandList")]
    public static partial void ResetCommandList(FCmdListH CommandList);

    [NativeCall("LuminaSharp_RHI_Submit")]
    private static partial void SubmitOne(FCmdListH CommandList, EQueueType Type);

    /// <summary>Submit one command list. <paramref name="Type"/> defaults to the graphics queue.</summary>
    public static void Submit(FCmdListH CommandList, EQueueType Type = EQueueType.Default) => SubmitOne(CommandList, Type);

    [NativeCall("LuminaSharp_RHI_SubmitLists")]
    private static partial void SubmitListsCore(EQueueType Queue, ReadOnlySpan<FCmdListH> CommandLists,
        ReadOnlySpan<FSemaphoreInfo> Waits, ReadOnlySpan<FSemaphoreInfo> Signals);

    /// <summary>Submit command lists with optional timeline-semaphore waits/signals.</summary>
    public static void Submit(EQueueType Queue, ReadOnlySpan<FCmdListH> CommandLists,
        ReadOnlySpan<FSemaphoreInfo> Waits, ReadOnlySpan<FSemaphoreInfo> Signals)
        => SubmitListsCore(Queue, CommandLists, Waits, Signals);

    // ---- Device / memory introspection ----

    [NativeCall("LuminaSharp_RHI_GetMemoryTotals")]
    public static partial FGPUMemoryTotals GetMemoryTotals();

    [NativeCall("LuminaSharp_RHI_GetMemoryHeap")]
    public static partial FGPUMemoryHeapStats GetMemoryHeap(int HeapIndex);

    /// <summary>Full per-heap GPU memory breakdown.</summary>
    public static (FGPUMemoryTotals Totals, FGPUMemoryHeapStats[] Heaps) GetGPUMemoryStats()
    {
        FGPUMemoryTotals Totals = GetMemoryTotals();
        FGPUMemoryHeapStats[] Heaps = new FGPUMemoryHeapStats[Totals.HeapCount];
        for (int i = 0; i < Heaps.Length; i++)
        {
            Heaps[i] = GetMemoryHeap(i);
        }
        return (Totals, Heaps);
    }

    [NativeCall("LuminaSharp_RHI_GetDeviceName")]
    private static partial string GetDeviceName();
    [NativeCall("LuminaSharp_RHI_GetDeviceAPIName")]
    private static partial string GetDeviceAPIName();
    [NativeCall("LuminaSharp_RHI_GetDeviceIsDiscrete")]
    private static partial bool GetDeviceIsDiscrete();

    /// <summary>API-neutral GPU device summary.</summary>
    public static GPUDeviceInfo GetDeviceInfo() => new(GetDeviceName(), GetDeviceAPIName(), GetDeviceIsDiscrete());

}

/// <summary>
/// Runtime support layer for the RHI (C++ <c>RHI::Core</c>): the global texture/sampler heap, the per-frame
/// transient ring, deferred frees, and pipeline creation from the engine shader library by name.
/// </summary>
public static unsafe partial class RHICore
{
    /// <summary>The engine's global texture/sampler heap.</summary>
    [NativeCall("LuminaSharp_RHI_CoreGetGlobalHeap")]
    public static partial FTextureHeapH GetGlobalHeap();

    /// <summary>
    /// Per-frame transient GPU memory (CPU-write, device-addressable); valid until the slot recycles.
    /// <paramref name="Alignment"/> defaults to 16 bytes.
    /// </summary>
    [NativeCall("LuminaSharp_RHI_CoreAllocTransient")]
    public static partial FTransientAlloc AllocTransient(ulong Size, ulong Alignment = 16);

    /// <summary>Free GPU memory once every in-flight frame has retired.</summary>
    [NativeCall("LuminaSharp_RHI_CoreDeferredFree")]
    public static partial void DeferredFree(GPUPtr Memory);

    /// <summary>Build a graphics pipeline from named shaders in the engine shader library (compiles/caches).</summary>
    [NativeCall("LuminaSharp_RHI_CoreCreateGraphicsPipeline")]
    public static partial FPipelineH CreateGraphicsPipeline(string VertexShader, string PixelShader,
        FRasterDesc Raster, ReadOnlySpan<FColorTarget> ColorTargets);

    /// <summary>Build a compute pipeline from a named shader in the engine shader library.</summary>
    [NativeCall("LuminaSharp_RHI_CoreCreateComputePipeline")]
    public static partial FPipelineH CreateComputePipeline(string ComputeShader);
}

/// <summary>API-neutral GPU device summary (RHI::FGPUDeviceInfo).</summary>
public sealed class GPUDeviceInfo
{
    public string Name { get; }       // e.g. "NVIDIA GeForce RTX 4080"
    public string APIName { get; }    // e.g. "Vulkan 1.4.250"
    public bool IsDiscrete { get; }

    internal GPUDeviceInfo(string Name, string APIName, bool IsDiscrete)
    {
        this.Name = Name;
        this.APIName = APIName;
        this.IsDiscrete = IsDiscrete;
    }

    public override string ToString() => $"{Name} ({APIName}{(IsDiscrete ? ", discrete" : "")})";
}
