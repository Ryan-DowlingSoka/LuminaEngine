using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// Snapshot of C# runtime state for the editor's C# Diagnostics tool: managed heap, GC counters, and
/// the collectible-ALC / script-generation health that reveals a hot-reload unload leak. Native owns the
/// storage; <see cref="Host.GetRuntimeDiagnostics"/> writes through a pointer to it. The layout mirrors
/// the native Lumina::DotNet::FScriptDiagnostics field-for-field (Sequential, 8-byte fields first) — keep
/// the two in sync. Editor-only consumer; nothing on the runtime path reads it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal struct FScriptDiagnostics
{
    public long   ManagedHeapBytes;     // GC.GetTotalMemory(false)
    public long   HeapSizeBytes;        // GCMemoryInfo.HeapSizeBytes
    public long   FragmentedBytes;      // GCMemoryInfo.FragmentedBytes
    public long   CommittedBytes;       // GCMemoryInfo.TotalCommittedBytes
    public long   TotalAllocatedBytes;  // GC.GetTotalAllocatedBytes() (lifetime; drives the churn rate)
    public long   WorkingSetBytes;      // Environment.WorkingSet (whole-process)
    public double PauseTimePercentage;  // GCMemoryInfo.PauseTimePercentage
    public double LastPauseMs;          // last GC pause duration

    public int    Gen0Collections;
    public int    Gen1Collections;
    public int    Gen2Collections;
    public int    PinnedObjects;        // GCMemoryInfo.PinnedObjectsCount
    public int    Generation;           // current script generation
    public int    EntityScriptCount;
    public int    EntitySystemCount;
    public int    LoadedTypeCount;
    public int    AliveScriptAlcCount;  // collectible GameScripts.Gen* contexts still loaded (1 == healthy)
    public int    OldestAliveGeneration;// lowest still-resident generation (0 if none)
    public int    ScriptsOnline;        // 1 when a generation is loaded
    public int    Reserved;             // pad to an 8-byte multiple
}
