using System;
using System.Runtime.InteropServices;
using LuminaSharp;

namespace Lumina;

/// Hand-written ergonomic half of SDynamicMeshComponent (the rest is Reflector-generated). Set streams then Commit; positions and indices required, normals derived if absent. Call on the game thread.
public unsafe partial class SDynamicMeshComponent
{
    /// Vertex positions (one per vertex). Required.
    public void SetPositions(ReadOnlySpan<FVector3> Positions)
        => SetPositionsRaw(MemoryMarshal.Cast<FVector3, float>(Positions));

    /// Vertex normals (one per vertex); optional, smooth normals are derived when omitted or mismatched.
    public void SetNormals(ReadOnlySpan<FVector3> Normals)
        => SetNormalsRaw(MemoryMarshal.Cast<FVector3, float>(Normals));

    /// Vertex texture coordinates (one per vertex). Optional (defaults to zero).
    public void SetUVs(ReadOnlySpan<FVector2> UVs)
        => SetUVsRaw(MemoryMarshal.Cast<FVector2, float>(UVs));

    /// Per-vertex colors as linear RGBA floats. Optional (defaults to white).
    public void SetColors(ReadOnlySpan<FVector4> Colors)
        => SetColorsFloatRaw(MemoryMarshal.Cast<FVector4, float>(Colors));

    /// Per-vertex colors as pre-packed RGBA8 (0xAABBGGRR). Optional (defaults to white).
    public void SetColors(ReadOnlySpan<uint> PackedColors)
        => SetColorsPackedRaw(PackedColors);

    /// Triangle indices (3 per triangle), referencing the vertex streams.
    public void SetIndices(ReadOnlySpan<int> Indices)
        => SetIndicesRaw(MemoryMarshal.Cast<int, uint>(Indices));

    /// Triangle indices (3 per triangle), referencing the vertex streams.
    public void SetIndices(ReadOnlySpan<uint> Indices)
        => SetIndicesRaw(Indices);

    // Flat shims (Runtime module). The component Handle is the first native argument; each span expands to (T*, int).

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_DynMesh_SetPositions")]
    private partial void SetPositionsRaw(ReadOnlySpan<float> Data);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_DynMesh_SetNormals")]
    private partial void SetNormalsRaw(ReadOnlySpan<float> Data);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_DynMesh_SetUVs")]
    private partial void SetUVsRaw(ReadOnlySpan<float> Data);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_DynMesh_SetColorsFloat")]
    private partial void SetColorsFloatRaw(ReadOnlySpan<float> Data);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_DynMesh_SetColorsPacked")]
    private partial void SetColorsPackedRaw(ReadOnlySpan<uint> Data);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_DynMesh_SetIndices")]
    private partial void SetIndicesRaw(ReadOnlySpan<uint> Data);
}
