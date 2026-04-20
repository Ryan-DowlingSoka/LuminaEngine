#include "PCH.h"
#include "TerrainSculptSystem.h"
#include "TaskSystem/TaskSystem.h"
#include "World/Entity/Components/TerrainComponent.h"

namespace Lumina
{
    namespace
    {
        // Terrain renderer places sample(0,0) at TerrainOrigin.xz, HalfSize in world
        // space (see ForwardRenderScene OriginXZ). Sculpt math must use the same
        // offset so the brush affects the samples the user actually sees under the cursor.
        glm::vec2 TerrainOriginXZ(const STerrainComponent& Terrain, const glm::vec3& TerrainOrigin)
        {
            const float HalfSize = Terrain.TileWorldSize * 0.5f;
            return glm::vec2(TerrainOrigin.x - HalfSize, TerrainOrigin.z - HalfSize);
        }

        // World-space footprint mapped to heightmap sample bounds, clipped to the grid.
        // Returns (MinX, MinY, MaxX, MaxY) inclusive or a zero-area rect when outside.
        glm::ivec4 ComputeSampleRect(const STerrainComponent& Terrain, const glm::vec3& TerrainOrigin, const glm::vec3& WorldPos, float Radius)
        {
            const float Stride = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
            const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, TerrainOrigin);
            const float LocalX = WorldPos.x - OriginXZ.x;
            const float LocalZ = WorldPos.z - OriginXZ.y;

            int MinX = int(std::floor((LocalX - Radius) / Stride));
            int MaxX = int(std::ceil ((LocalX + Radius) / Stride));
            int MinY = int(std::floor((LocalZ - Radius) / Stride));
            int MaxY = int(std::ceil ((LocalZ + Radius) / Stride));

            MinX = std::max(MinX, 0);
            MinY = std::max(MinY, 0);
            MaxX = std::min(MaxX, Terrain.Resolution - 1);
            MaxY = std::min(MaxY, Terrain.Resolution - 1);

            if (MaxX < MinX || MaxY < MinY)
            {
                return glm::ivec4(0, 0, -1, -1);
            }

            return glm::ivec4(MinX, MinY, MaxX, MaxY);
        }

        // Smoothstep-shaped disk falloff. Returns 0 outside the radius.
        float BrushWeight(float DistSq, float Radius, float Falloff)
        {
            const float R  = Radius;
            const float R2 = R * R;
            if (DistSq >= R2)
            {
                return 0.0f;
            }

            float T = 1.0f - std::sqrt(DistSq) / R;           // 1 at center, 0 at edge
            float H = glm::mix(1.0f, T * T * (3.0f - 2.0f * T), Falloff);
            return H;
        }
    }

    glm::ivec2 FTerrainSculptSystem::WorldToSample(const STerrainComponent& Terrain, const glm::vec3& WorldPos)
    {
        const float Stride = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
        return glm::ivec2(int(WorldPos.x / Stride), int(WorldPos.z / Stride));
    }

    bool FTerrainSculptSystem::Raycast(const STerrainComponent& Terrain, const glm::vec3& TerrainOrigin, const glm::vec3& RayOrigin, const glm::vec3& RayDir, glm::vec3& OutHit)
    {
        const float MaxDist = Terrain.TileWorldSize * 4.0f;
        const float Step    = std::max(0.5f, Terrain.TileWorldSize / float(Terrain.Resolution) * 0.5f);

        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, TerrainOrigin);
        const float BaseY  = TerrainOrigin.y;
        const float Stride = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
        
        glm::vec3 Pos = RayOrigin;
        bool bWasAbove = false;

        for (float T = 0.0f; T < MaxDist; T += Step)
        {
            int SX = int((Pos.x - OriginXZ.x) / Stride);
            int SY = int((Pos.z - OriginXZ.y) / Stride);
            if (SX < 0 || SY < 0 || SX >= Terrain.Resolution || SY >= Terrain.Resolution)
            {
                Pos += RayDir * Step;
                bWasAbove = false;
                continue;
            }

            const float Sample = Terrain.Heightmap[SY * Terrain.Resolution + SX];
            const float TerrainY = BaseY + Sample * Terrain.MaxHeight;
            if (Pos.y > TerrainY)
            {
                bWasAbove = true;
            }
            else if (bWasAbove)
            {
                OutHit = glm::vec3(Pos.x, TerrainY, Pos.z);
                return true;
            }
            Pos += RayDir * Step;
        }
        return false;
    }

    void FTerrainSculptSystem::ApplyDab(STerrainComponent& Terrain, const FTerrainSculptDab& Dab)
    {
        if (Terrain.Resolution <= 0 || Terrain.Heightmap.empty())
        {
            return;
        }

        const glm::ivec4 Rect = ComputeSampleRect(Terrain, Dab.TerrainOrigin, Dab.WorldPosition, Dab.Radius);
        if (Rect.z < Rect.x || Rect.w < Rect.y)
        {
            return;
        }

        switch (Dab.Mode)
        {
        case ETerrainBrushMode::Sculpt:  ApplySculpt (Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Flatten: ApplyFlatten(Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Smooth:  ApplySmooth (Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Paint:   ApplyPaint  (Terrain, Dab, Rect); break;
        }

        if (Dab.Mode == ETerrainBrushMode::Paint)
        {
            const int32 Layer = Dab.ActiveLayer;
            if (Layer >= 0 && Layer < (int32)Terrain.Layers.size())
            {
                Terrain.MarkWeightsDirty((uint32)Layer,
                                         glm::ivec2(Rect.x, Rect.y),
                                         glm::ivec2(Rect.z, Rect.w));
            }
        }
        else
        {
            Terrain.MarkHeightDirty(glm::ivec2(Rect.x, Rect.y), glm::ivec2(Rect.z, Rect.w));
        }
    }
    
    void FTerrainSculptSystem::ApplySculpt(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect)
    {
        const float Stride    = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
        const float Radius    = Dab.Radius;
        
        // Strength is expressed in normalized height units per second (i.e. 1.0 == full
        const float DeltaVal = (Dab.Strength * Dab.DeltaSeconds * static_cast<float>(Dab.SculptSign)) / Terrain.MaxHeight;
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Radius, DeltaVal, Stride, OriginXZ](const Task::FParallelRange& Range)
        {
            for (uint32 RowIdx = Range.Start; RowIdx < Range.End; ++RowIdx)
            {
                int Y = Rect.y + static_cast<int>(RowIdx);
                float WorldZ = OriginXZ.y + static_cast<float>(Y) * Stride;
                float DZ = WorldZ - Dab.WorldPosition.z;
                for (int X = Rect.x; X <= Rect.z; ++X)
                {
                    float WorldX = OriginXZ.x + static_cast<float>(X) * Stride;
                    float DX = WorldX - Dab.WorldPosition.x;
                    float DistSq = DX * DX + DZ * DZ;
                    float W = BrushWeight(DistSq, Radius, Dab.Falloff);
                    if (W <= 0.0f)
                    {
                        continue;
                    }

                    float& H = Terrain.Heightmap[Y * Terrain.Resolution + X];
                    H = glm::clamp(H + DeltaVal * W, 0.0f, 1.0f);
                }
            }
        }, 64u);
    }

    void FTerrainSculptSystem::ApplyFlatten(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect)
    {
        const float Stride = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
        const float Target = Terrain.MaxHeight > 0.0f ? glm::clamp(Dab.FlattenHeight / Terrain.MaxHeight, 0.0f, 1.0f) : 0.0f;
        const float Speed  = glm::clamp(Dab.Strength * Dab.DeltaSeconds, 0.0f, 1.0f);
        const float Radius = Dab.Radius;
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Radius, Target, Speed, Stride, OriginXZ](const Task::FParallelRange& Range)
        {
            for (uint32 RowIdx = Range.Start; RowIdx < Range.End; ++RowIdx)
            {
                int Y = Rect.y + int(RowIdx);
                float WorldZ = OriginXZ.y + float(Y) * Stride;
                float DZ = WorldZ - Dab.WorldPosition.z;
                for (int X = Rect.x; X <= Rect.z; ++X)
                {
                    float WorldX = OriginXZ.x + float(X) * Stride;
                    float DX = WorldX - Dab.WorldPosition.x;
                    float W = BrushWeight(DX * DX + DZ * DZ, Radius, Dab.Falloff);
                    if (W <= 0.0f)
                    {
                        continue;
                    }

                    float& H = Terrain.Heightmap[Y * Terrain.Resolution + X];
                    H = glm::mix(H, Target, Speed * W);
                }
            }
        }, 64u);
    }

    void FTerrainSculptSystem::ApplySmooth(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect)
    {
        // 3x3 box-blur read against the previous frame. Because rows are disjoint on
        // write, sampling neighbors on the same snapshot is race-free inside a dab.
        const float Stride = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
        const float Speed  = glm::clamp(Dab.Strength * Dab.DeltaSeconds, 0.0f, 1.0f);
        const float Radius = Dab.Radius;
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        TVector<float> Snapshot = Terrain.Heightmap;

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Radius, Speed, Stride, OriginXZ](const Task::FParallelRange& Range)
        {
            const int32 Res = Terrain.Resolution;
            for (uint32 RowIdx = Range.Start; RowIdx < Range.End; ++RowIdx)
            {
                int Y = Rect.y + int(RowIdx);
                float WorldZ = OriginXZ.y + float(Y) * Stride;
                float DZ = WorldZ - Dab.WorldPosition.z;
                for (int X = Rect.x; X <= Rect.z; ++X)
                {
                    float WorldX = OriginXZ.x + float(X) * Stride;
                    float DX = WorldX - Dab.WorldPosition.x;
                    float W = BrushWeight(DX * DX + DZ * DZ, Radius, Dab.Falloff);
                    if (W <= 0.0f)
                    {
                        continue;
                    }

                    float Sum = 0.0f;
                    int Count = 0;
                    for (int YY = std::max(Y - 1, 0); YY <= std::min(Y + 1, Res - 1); ++YY)
                    {
                        for (int XX = std::max(X - 1, 0); XX <= std::min(X + 1, Res - 1); ++XX)
                        {
                            Sum += Snapshot[YY * Res + XX];
                            ++Count;
                        }
                    }
                    float Avg = Sum / float(Count);

                    float& H = Terrain.Heightmap[Y * Res + X];
                    H = glm::mix(H, Avg, Speed * W);
                }
            }
        }, 64u);
    }

    void FTerrainSculptSystem::ApplyPaint(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect)
    {
        const int32 Layer = Dab.ActiveLayer;
        if (Layer < 0 || Layer >= (int32)Terrain.Layers.size())
            return;

        const int32 Res         = Terrain.Resolution;
        const int32 LayerCount  = (int32)Terrain.Layers.size();
        const int32 LayerStride = Res * Res;
        const size_t Needed = size_t(LayerCount) * size_t(LayerStride);
        if (Terrain.LayerWeights.size() < Needed)
        {
            Terrain.LayerWeights.resize(Needed, 0u);
        }

        const float Stride = Terrain.TileWorldSize / float(Terrain.Resolution - 1);
        const float Speed  = glm::clamp(Dab.Strength * Dab.DeltaSeconds, 0.0f, 1.0f);
        const float Radius = Dab.Radius;
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Radius, Speed, Stride, OriginXZ, Layer, LayerStride, Res](const Task::FParallelRange& Range)
        {
            for (uint32 RowIdx = Range.Start; RowIdx < Range.End; ++RowIdx)
            {
                int Y = Rect.y + int(RowIdx);
                float WorldZ = OriginXZ.y + float(Y) * Stride;
                float DZ = WorldZ - Dab.WorldPosition.z;
                for (int X = Rect.x; X <= Rect.z; ++X)
                {
                    float WorldX = OriginXZ.x + float(X) * Stride;
                    float DX = WorldX - Dab.WorldPosition.x;
                    float W = BrushWeight(DX * DX + DZ * DZ, Radius, Dab.Falloff);
                    if (W <= 0.0f)
                    {
                        continue;
                    }

                    // Paint nudges only the active layer — toward 1.0 normally,
                    // toward 0.0 while Shift is held (erase). The shader normalises
                    // weights at sample time so the visual blend with every other
                    // layer is purely a function of relative weight.
                    const size_t Pixel = size_t(Y) * size_t(Res) + size_t(X);
                    uint8& Target  = Terrain.LayerWeights[size_t(Layer) * LayerStride + Pixel];
                    float  Current = float(Target) / 255.0f;
                    float  Goal    = (Dab.SculptSign >= 0) ? 1.0f : 0.0f;
                    float  New     = glm::mix(Current, Goal, Speed * W);
                    Target = uint8(glm::clamp(New * 255.0f + 0.5f, 0.0f, 255.0f));
                }
            }
        }, 64u);
    }
}
