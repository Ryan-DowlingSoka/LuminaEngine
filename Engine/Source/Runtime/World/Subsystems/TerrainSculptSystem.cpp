#include "PCH.h"
#include "TerrainSculptSystem.h"
#include "TaskSystem/TaskSystem.h"
#include "World/Entity/Components/TerrainComponent.h"

namespace Lumina
{
    namespace
    {
        // Matches ForwardRenderScene OriginXZ so brush hits the same samples the renderer sees.
        glm::vec2 TerrainOriginXZ(const STerrainComponent& Terrain, const glm::vec3& TerrainOrigin)
        {
            const float HalfSize = Terrain.TileWorldSize * 0.5f;
            return glm::vec2(TerrainOrigin.x - HalfSize, TerrainOrigin.z - HalfSize);
        }

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

        glm::ivec4 Rect;
        if (Dab.Mode == ETerrainBrushMode::Ramp)
        {
                const glm::ivec4 R1 = ComputeSampleRect(Terrain, Dab.TerrainOrigin, Dab.RampStart, Dab.RampHalfWidth);
            const glm::ivec4 R2 = ComputeSampleRect(Terrain, Dab.TerrainOrigin, Dab.RampEnd,   Dab.RampHalfWidth);
            const bool V1 = !(R1.z < R1.x || R1.w < R1.y);
            const bool V2 = !(R2.z < R2.x || R2.w < R2.y);
            if (!V1 && !V2)
            {
                return;
            }
            if (V1 && V2)
            {
                Rect = glm::ivec4(std::min(R1.x, R2.x), std::min(R1.y, R2.y), std::max(R1.z, R2.z), std::max(R1.w, R2.w));
            }
            else
            {
                Rect = V1 ? R1 : R2;
            }
        }
        else
        {
            Rect = ComputeSampleRect(Terrain, Dab.TerrainOrigin, Dab.WorldPosition, Dab.Radius);
        }
        if (Rect.z < Rect.x || Rect.w < Rect.y)
        {
            return;
        }

        switch (Dab.Mode)
        {
        case ETerrainBrushMode::Sculpt:  ApplySculpt (Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Flatten: ApplyFlatten(Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Smooth:  ApplySmooth (Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Noise:   ApplyNoise  (Terrain, Dab, Rect); break;
        case ETerrainBrushMode::Ramp:    ApplyRamp   (Terrain, Dab, Rect); break;
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
        // Snapshot only the affected rect+1px apron; copying the full map at 1025^2 dominated per-dab cost.
        const int32 Res    = Terrain.Resolution;
        const float Stride = Terrain.TileWorldSize / float(Res - 1);
        const float Speed  = glm::clamp(Dab.Strength * Dab.DeltaSeconds, 0.0f, 1.0f);
        const float Radius = Dab.Radius;
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        const int32 SnapMinX = std::max(Rect.x - 1, 0);
        const int32 SnapMinY = std::max(Rect.y - 1, 0);
        const int32 SnapMaxX = std::min(Rect.z + 1, Res - 1);
        const int32 SnapMaxY = std::min(Rect.w + 1, Res - 1);
        const int32 SnapW    = SnapMaxX - SnapMinX + 1;
        const int32 SnapH    = SnapMaxY - SnapMinY + 1;

        TVector<float> Snapshot;
        Snapshot.resize(size_t(SnapW) * size_t(SnapH));
        for (int32 Y = 0; Y < SnapH; ++Y)
        {
            const float* SrcRow = &Terrain.Heightmap[size_t(SnapMinY + Y) * size_t(Res) + size_t(SnapMinX)];
            float*       DstRow = &Snapshot[size_t(Y) * size_t(SnapW)];
            std::memcpy(DstRow, SrcRow, size_t(SnapW) * sizeof(float));
        }

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Radius, Speed, Stride, OriginXZ, SnapMinX, SnapMinY, SnapW](const Task::FParallelRange& Range)
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

                    float Sum = 0.0f;
                    int Count = 0;
                    for (int YY = std::max(Y - 1, 0); YY <= std::min(Y + 1, Res - 1); ++YY)
                    {
                        for (int XX = std::max(X - 1, 0); XX <= std::min(X + 1, Res - 1); ++XX)
                        {
                            Sum += Snapshot[size_t(YY - SnapMinY) * size_t(SnapW) + size_t(XX - SnapMinX)];
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

    namespace
    {
        inline float Hash2D(int X, int Y)
        {
            uint32 N = uint32(X) * 374761393u + uint32(Y) * 668265263u;
            N = (N ^ (N >> 13u)) * 1274126177u;
            N = N ^ (N >> 16u);
            return float(N & 0x00FFFFFFu) / float(0x00FFFFFF);
        }

        inline float ValueNoise2D(float X, float Y)
        {
            const int Xi = int(std::floor(X));
            const int Yi = int(std::floor(Y));
            const float Fx = X - float(Xi);
            const float Fy = Y - float(Yi);
            const float Ux = Fx * Fx * (3.0f - 2.0f * Fx);
            const float Uy = Fy * Fy * (3.0f - 2.0f * Fy);
            const float V00 = Hash2D(Xi,     Yi);
            const float V10 = Hash2D(Xi + 1, Yi);
            const float V01 = Hash2D(Xi,     Yi + 1);
            const float V11 = Hash2D(Xi + 1, Yi + 1);
            const float A = glm::mix(V00, V10, Ux);
            const float B = glm::mix(V01, V11, Ux);
            return glm::mix(A, B, Uy) * 2.0f - 1.0f;   // [-1, 1]
        }

        inline float Fbm2D(float X, float Y, int Octaves)
        {
            float Sum = 0.0f;
            float Amp = 1.0f;
            float Freq = 1.0f;
            float Norm = 0.0f;
            for (int i = 0; i < Octaves; ++i)
            {
                Sum  += ValueNoise2D(X * Freq, Y * Freq) * Amp;
                Norm += Amp;
                Amp  *= 0.5f;
                Freq *= 2.0f;
            }
            return (Norm > 0.0f) ? (Sum / Norm) : 0.0f;
        }
    }

    void FTerrainSculptSystem::ApplyNoise(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect)
    {
        const int32 Res    = Terrain.Resolution;
        const float Stride = Terrain.TileWorldSize / float(Res - 1);
        const float Radius = Dab.Radius;
        const float DeltaPerSecond = (Dab.Strength * Dab.DeltaSeconds * float(Dab.SculptSign)) / Terrain.MaxHeight;
        const float Freq = std::max(Dab.NoiseFrequency, 1e-6f);
        const int   Octaves = glm::clamp(Dab.NoiseOctaves, 1, 8);
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Radius, DeltaPerSecond, Stride, OriginXZ, Freq, Octaves](const Task::FParallelRange& Range)
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

                    const float N = Fbm2D(WorldX * Freq, WorldZ * Freq, Octaves);
                    float& H = Terrain.Heightmap[Y * Res + X];
                    H = glm::clamp(H + DeltaPerSecond * W * N, 0.0f, 1.0f);
                }
            }
        }, 64u);
    }

    void FTerrainSculptSystem::ApplyRamp(STerrainComponent& Terrain, const FTerrainSculptDab& Dab, const glm::ivec4& Rect)
    {
        const int32 Res    = Terrain.Resolution;
        const float Stride = Terrain.TileWorldSize / float(Res - 1);
        const float Speed  = glm::clamp(Dab.Strength * Dab.DeltaSeconds, 0.0f, 1.0f);
        const glm::vec2 OriginXZ = TerrainOriginXZ(Terrain, Dab.TerrainOrigin);

        const glm::vec2 A   = glm::vec2(Dab.RampStart.x, Dab.RampStart.z);
        const glm::vec2 B   = glm::vec2(Dab.RampEnd.x,   Dab.RampEnd.z);
        const glm::vec2 AB  = B - A;
        const float ABLen2  = glm::dot(AB, AB);
        if (ABLen2 < 1e-3f)
        {
            return;
        }

        const float MaxH = std::max(Terrain.MaxHeight, 1e-3f);
        const float StartN = Dab.RampUseExplicitHeights
            ? glm::clamp(Dab.RampStartHeight / MaxH, 0.0f, 1.0f)
            : glm::clamp(Dab.RampStart.y / MaxH, 0.0f, 1.0f);
        const float EndN = Dab.RampUseExplicitHeights
            ? glm::clamp(Dab.RampEndHeight / MaxH, 0.0f, 1.0f)
            : glm::clamp(Dab.RampEnd.y / MaxH, 0.0f, 1.0f);

        const float HalfWidth = std::max(Dab.RampHalfWidth, Stride);

        const int32 RowCount = Rect.w - Rect.y + 1;
        Task::ParallelFor((uint32)RowCount, [&, Rect, Speed, Stride, OriginXZ, A, AB, ABLen2, StartN, EndN, HalfWidth](const Task::FParallelRange& Range)
        {
            for (uint32 RowIdx = Range.Start; RowIdx < Range.End; ++RowIdx)
            {
                int Y = Rect.y + int(RowIdx);
                float WorldZ = OriginXZ.y + float(Y) * Stride;
                for (int X = Rect.x; X <= Rect.z; ++X)
                {
                    float WorldX = OriginXZ.x + float(X) * Stride;

                    const glm::vec2 P  = glm::vec2(WorldX, WorldZ) - A;
                    const float T      = glm::clamp(glm::dot(P, AB) / ABLen2, 0.0f, 1.0f);
                    const glm::vec2 C  = AB * T;
                    const float Perp2  = glm::dot(P - C, P - C);
                    const float Perp   = std::sqrt(Perp2);
                    if (Perp > HalfWidth)
                    {
                        continue;
                    }

                    const float Edge = Perp / HalfWidth;       // 0 center, 1 edge
                    const float Lat  = 1.0f - Edge;            // lateral weight
                    const float Soft = glm::mix(1.0f, Lat * Lat * (3.0f - 2.0f * Lat), Dab.Falloff);

                    const float Target = glm::mix(StartN, EndN, T);
                    float& H = Terrain.Heightmap[Y * Res + X];
                    H = glm::mix(H, Target, Speed * Soft);
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
