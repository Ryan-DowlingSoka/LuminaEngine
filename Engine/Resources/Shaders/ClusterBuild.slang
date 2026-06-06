#pragma once
#include "Includes/SceneGlobals.slang"

// Grid params are read from the per-frame scene snapshot (uSceneData), NOT live render-thread
// state. LightCull (which fills these clusters) and the base pass (which looks up the tiles) both
// index off the same snapshot frustum/screen size; sourcing the AABBs from live projection/screen
// size instead would bin lights into a different frustum than the lookup -> whole tiles light with
// the wrong lights whenever live and snapshot diverge (camera motion between extract and render).
float3 ScreenToView(float2 ScreenCoord)
{
    float2 UV       = ScreenCoord / float2(uSceneData.ScreenSize.xy);
    float4 NDC      = float4(UV * 2.0 - 1.0, 1.0, 1.0);
    float4 ViewCoord = mul(GetInverseCameraProjection(), NDC);
    ViewCoord       /= ViewCoord.w;
    return ViewCoord.xyz;
}

float3 LineIntersectionWithZPlane(float3 StartPoint, float3 EndPoint, float ZDistance)
{
    float3 Direction = EndPoint - StartPoint;
    float3 Normal    = float3(0.0, 0.0, -1.0);
    float  T         = (ZDistance - dot(Normal, StartPoint)) / dot(Normal, Direction);
    return StartPoint + T * Direction;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 DispatchID : SV_DispatchThreadID)
{
    uint3 GridSize   = uSceneData.GridSize.xyz;
    uint2 ScreenSize = uSceneData.ScreenSize.xy;

    uint TileIndex  = DispatchID.x;
    uint TotalTiles = GridSize.x * GridSize.y * GridSize.z;
    if (TileIndex >= TotalTiles)
        return;

    uint SliceSize = GridSize.x * GridSize.y;
    uint3 Tile;
    Tile.z = TileIndex / SliceSize;
    uint SliceRem = TileIndex - Tile.z * SliceSize;
    Tile.y = SliceRem / GridSize.x;
    Tile.x = SliceRem - Tile.y * GridSize.x;

    float ZNear = GetNearPlane();
    float ZFar  = GetFarPlane();

    float2 TileSize   = float2(ScreenSize) / float2(GridSize.xy);
    float2 MinTileSS  = float2(Tile.xy) * TileSize;
    float2 MaxTileSS  = float2(Tile.xy + 1) * TileSize;

    float3 MinTile = ScreenToView(MinTileSS);
    float3 MaxTile = ScreenToView(MaxTileSS);

    float PlaneNear = -ZNear * pow(ZFar / ZNear,  float(Tile.z)      / float(GridSize.z));
    float PlaneFar  = -ZNear * pow(ZFar / ZNear,  float(Tile.z + 1)  / float(GridSize.z));

    float3 MinPointNear = LineIntersectionWithZPlane(float3(0.0), MinTile, PlaneNear);
    float3 MinPointFar  = LineIntersectionWithZPlane(float3(0.0), MinTile, PlaneFar);
    float3 MaxPointNear = LineIntersectionWithZPlane(float3(0.0), MaxTile, PlaneNear);
    float3 MaxPointFar  = LineIntersectionWithZPlane(float3(0.0), MaxTile, PlaneFar);

    float3 MinBounds = min(min(MinPointNear, MinPointFar), min(MaxPointNear, MaxPointFar));
    float3 MaxBounds = max(max(MinPointNear, MinPointFar), max(MaxPointNear, MaxPointFar));

    uClusters[TileIndex].MinPoint = float4(MinBounds, 0.0);
    uClusters[TileIndex].MaxPoint = float4(MaxBounds, 0.0);
}