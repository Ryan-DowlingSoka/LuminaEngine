using System;
using System.Collections.Generic;
using LuminaSharp;
using Lumina;

namespace Game;

/// <summary>
/// Builds a cube from raw vertex data at runtime via <see cref="SDynamicMeshComponent"/>. Add a "C# Script"
/// component to an empty entity, select this script, press Play. Demonstrates the data-driven mesh API you'd
/// use for procedural geometry, voxels, marching cubes, etc. - here a single static cube, but you can call
/// Commit() again every frame to animate the geometry.
/// </summary>
public sealed class DynamicMeshExample : EntityScript
{
    [Property(Tooltip = "Half-extent of the generated cube, in world units.")]
    public float Size = 1.0f;

    public override void OnReady()
    {
        SDynamicMeshComponent Mesh = Registry.GetOrAdd<SDynamicMeshComponent>(Entity)!;
        BuildCube(Mesh, Size);
    }

    private static void BuildCube(SDynamicMeshComponent Mesh, float HalfExtent)
    {
        // Six faces, 4 unique vertices each (so every face gets flat normals + its own UVs).
        var Positions = new List<FVector3>();
        var Normals = new List<FVector3>();
        var UVs = new List<FVector2>();
        var Indices = new List<int>();

        void AddFace(FVector3 Origin, FVector3 Right, FVector3 Up)
        {
            int Base = Positions.Count;
            FVector3 Normal = FVector3.Cross(Right, Up).Normalized();

            Positions.Add(Origin);
            Positions.Add(Origin + Right);
            Positions.Add(Origin + Right + Up);
            Positions.Add(Origin + Up);

            for (int i = 0; i < 4; ++i)
            {
                Normals.Add(Normal);
            }

            UVs.Add(new FVector2(0, 0));
            UVs.Add(new FVector2(1, 0));
            UVs.Add(new FVector2(1, 1));
            UVs.Add(new FVector2(0, 1));

            Indices.Add(Base + 0);
            Indices.Add(Base + 1);
            Indices.Add(Base + 2);
            Indices.Add(Base + 0);
            Indices.Add(Base + 2);
            Indices.Add(Base + 3);
        }

        float S = HalfExtent;
        FVector3 X = new FVector3(2 * S, 0, 0);
        FVector3 Y = new FVector3(0, 2 * S, 0);
        FVector3 Z = new FVector3(0, 0, 2 * S);
        FVector3 C = new FVector3(-S, -S, -S); // min corner

        // (Origin, Right, Up) chosen so cross(Right, Up) points outward, giving CCW front faces.
        AddFace(C + Z,     X, Y); // +Z front
        AddFace(C + X,    -X, Y); // -Z back
        AddFace(C,         Z, Y); // -X left
        AddFace(C + X,     Y, Z); // +X right
        AddFace(C + Y,     Z, X); // +Y top
        AddFace(C,         X, Z); // -Y bottom

        Mesh.ClearMesh();
        Mesh.SetPositions(Positions.ToArray());
        Mesh.SetNormals(Normals.ToArray());
        Mesh.SetUVs(UVs.ToArray());
        Mesh.SetIndices(Indices.ToArray());
        Mesh.Commit();
    }
}
