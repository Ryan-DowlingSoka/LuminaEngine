#include "pch.h"
#include "NavMeshSystem.h"

#include "AI/Navigation/NavMesh.h"
#include "AI/Navigation/NavMeshBuilder.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Core/Console/ConsoleVariable.h"
#include "Renderer/MeshData.h"
#include "Renderer/Vertex.h"
#include "TaskSystem/TaskSystem.h"
#include "World/Entity/Components/CharacterComponent.h"
#include "World/Entity/Components/NavMeshComponent.h"
#include "World/Entity/Components/PhysicsComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/World.h"
#include "Scripting/Lua/Reference.h"
#include "Scripting/Lua/Class.h"

namespace Lumina
{
    // NOLINTBEGIN(bugprone-throwing-static-initialization)

    // Master toggle. When false, no nav debug draws at all (sub-CVars are ignored).
    static TConsoleVar<bool>  CVarNavDrawDebug      ("Nav.DrawDebug",          false, "Master toggle for navmesh debug visualization.");

    // Per-layer toggles. Defaults aim for a useful "first look" when the master is flipped on.
    static TConsoleVar<bool>  CVarNavDebugEdges     ("Nav.Debug.Edges",        true,  "Draw detail-triangle edges (faint) and poly boundary edges (thick).");
    static TConsoleVar<bool>  CVarNavDebugColorArea ("Nav.Debug.ColorByArea",  true,  "Color edges/centers by ENavArea (ground/water/door/danger).");
    static TConsoleVar<bool>  CVarNavDebugVerts     ("Nav.Debug.Vertices",     false, "Sphere at every poly boundary vertex (intersections).");
    static TConsoleVar<bool>  CVarNavDebugCenters   ("Nav.Debug.Centers",      false, "Small sphere at every walkable triangle center.");
    static TConsoleVar<bool>  CVarNavDebugTiles     ("Nav.Debug.TileBounds",   true,  "Wireframe box for each loaded nav tile.");
    static TConsoleVar<bool>  CVarNavDebugBounds    ("Nav.Debug.BakeBounds",   true,  "Wireframe box for the bake volume (Center +/- Extents).");
    static TConsoleVar<bool>  CVarNavDebugLinks     ("Nav.Debug.OffMeshLinks", true,  "Arrows for off-mesh connections.");
    static TConsoleVar<bool>  CVarNavDebugLog       ("Nav.Debug.LogStats",     false, "Log triangle/edge/tile counts on every cache refresh.");
    static TConsoleVar<float> CVarNavDebugLift      ("Nav.Debug.LiftY",        0.05f, "Vertical offset added to debug geometry to avoid Z-fighting.");
    static TConsoleVar<float> CVarNavDebugVertSize  ("Nav.Debug.VertexRadius", 0.08f, "Radius of vertex spheres (also drives center-sphere size).");

    // NOLINTEND(bugprone-throwing-static-initialization)

    namespace
    {
        // Per-area colors for at-a-glance area classification. Alpha currently unused by the line batcher,
        // but kept consistent so future translucent rendering doesn't need a second table.
        FORCEINLINE FVector4 NavAreaColor(uint8 Area)
        {
            switch ((ENavArea)Area)
            {
                case ENavArea::Ground: return FVector4(0.20f, 0.85f, 0.30f, 1.0f);
                case ENavArea::Water:  return FVector4(0.20f, 0.45f, 0.95f, 1.0f);
                case ENavArea::Door:   return FVector4(0.95f, 0.65f, 0.15f, 1.0f);
                case ENavArea::Danger: return FVector4(0.95f, 0.20f, 0.20f, 1.0f);
                case ENavArea::Null:   return FVector4(0.40f, 0.40f, 0.40f, 1.0f);
                default:               return FVector4(0.75f, 0.30f, 0.95f, 1.0f);
            }
        }

        void DrawNavDebug(const FSystemContext& Context, const SNavMeshComponent& Comp)
        {
            const FNavMesh& Mesh = *Comp.Runtime.Mesh;
            const FVector3 Lift(0.0f, CVarNavDebugLift.GetValue(), 0.0f);
            const bool bColorByArea = CVarNavDebugColorArea.GetValue();

            if (CVarNavDebugBounds.GetValue())
            {
                Context.DrawDebugBox(Comp.Center, Comp.Extents, FQuat(1.0f, 0.0f, 0.0f, 0.0f),
                    FVector4(1.0f, 0.85f, 0.10f, 1.0f), 4.0f, -1.0f);
            }

            if (CVarNavDebugTiles.GetValue())
            {
                const FVector4 TileColor(0.20f, 0.65f, 1.0f, 1.0f);
                Mesh.ForEachLoadedTile([&Context, TileColor](const FNavTileBounds& T)
                {
                    const FVector3 Center = (T.Min + T.Max) * 0.5f;
                    const FVector3 Half   = (T.Max - T.Min) * 0.5f;
                    Context.DrawDebugBox(Center, Half, FQuat(1.0f, 0.0f, 0.0f, 0.0f), TileColor, 4.0f, -1.0f);
                });
            }

            if (CVarNavDebugEdges.GetValue())
            {
                // Faint interior triangle edges so the mesh fill reads, without drowning the boundary.
                Mesh.ParallelForEachTriangle([&Context, &Lift, bColorByArea](const FVector3& A, const FVector3& B, const FVector3& C, uint8 Area)
                {
                    FVector4 Color = bColorByArea ? NavAreaColor(Area) : FVector4(0.05f, 1.0f, 0.15f, 1.0f);
                    Color *= FVector4(0.6f, 0.6f, 0.6f, 1.0f);
                    constexpr float Thickness = 1.0f;
                    Context.DrawDebugLine(A + Lift, B + Lift, Color, Thickness, -1.0f);
                    Context.DrawDebugLine(B + Lift, C + Lift, Color, Thickness, -1.0f);
                    Context.DrawDebugLine(C + Lift, A + Lift, Color, Thickness, -1.0f);
                });

                // Poly perimeter on top, thicker and brighter so the actual nav polygon shape stands out.
                Mesh.ForEachBoundaryEdge([&Context, &Lift, bColorByArea](const FVector3& A, const FVector3& B, uint8 Area)
                {
                    const FVector4 Color = bColorByArea ? NavAreaColor(Area) : FVector4(0.0f, 0.95f, 1.0f, 1.0f);
                    Context.DrawDebugLine(A + Lift, B + Lift, Color, 3.0f, -1.0f);
                });
            }

            if (CVarNavDebugVerts.GetValue())
            {
                const float R = CVarNavDebugVertSize.GetValue();
                const FVector4 VColor(1.0f, 0.85f, 0.0f, 1.0f);
                // Boundary endpoints == poly vertices == the "intersections" users want to see.
                Mesh.ForEachBoundaryEdge([&Context, R, VColor, &Lift](const FVector3& A, const FVector3& B, uint8)
                {
                    Context.DrawDebugSphere(A + Lift, R, VColor, 8, 4.0f, -1.0f);
                    Context.DrawDebugSphere(B + Lift, R, VColor, 8, 4.0f, -1.0f);
                });
            }

            if (CVarNavDebugCenters.GetValue())
            {
                const float R = std::max(0.02f, CVarNavDebugVertSize.GetValue() * 0.5f);
                Mesh.ParallelForEachTriangle([&Context, R, &Lift, bColorByArea](const FVector3& A, const FVector3& B, const FVector3& C, uint8 Area)
                {
                    const FVector3 C0 = (A + B + C) * (1.0f / 3.0f) + Lift;
                    const FVector4 Color = bColorByArea ? NavAreaColor(Area) : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
                    Context.DrawDebugSphere(C0, R, Color, 6, 4.0f, -1.0f);
                });
            }

            if (CVarNavDebugLinks.GetValue())
            {
                const FVector4 LinkColor(1.0f, 0.30f, 0.95f, 1.0f);
                Mesh.ForEachOffMeshLink([&Context, LinkColor, &Lift](const FVector3& A, const FVector3& B)
                {
                    const FVector3 Dir = B - A;
                    const float Len = Math::Length(Dir);
                    if (Len > 1e-4f)
                    {
                        Context.DrawDebugArrow(A + Lift, Dir / Len, Len, LinkColor, 2.5f, -1.0f, 0.25f);
                    }
                    Context.DrawDebugSphere(A + Lift, 0.15f, LinkColor, 10, 4.0f, -1.0f);
                    Context.DrawDebugSphere(B + Lift, 0.15f, LinkColor, 10, 4.0f, -1.0f);
                });
            }
        }
    }

    namespace
    {
        struct FGatherAccumulator
        {
            TVector<FVector3> Vertices;
            TVector<uint32>    Indices;
            FVector3          AABBMin = FVector3( FLT_MAX);
            FVector3          AABBMax = FVector3(-FLT_MAX);
        };

        // 8-bit local indices packed 3 per uint32; vertex indices local to Meshlet.VertexOffset.
        FORCEINLINE void UnpackTri(uint32 Packed, uint32& A, uint32& B, uint32& C)
        {
            A = (Packed >> 0)  & 0xFFu;
            B = (Packed >> 8)  & 0xFFu;
            C = (Packed >> 16) & 0xFFu;
        }

        // 10-10-10 unsigned position; MeshOrigin + (LoInt + q) * GridStep.
        FORCEINLINE FVector3 DecodePosition(uint32 Packed, const FIntVector3& LoInt, const FVector3& MeshOrigin, const FVector3& GridStep)
        {
            const FIntVector3 Q(
                (int32)((Packed >>  0) & 0x3FFu),
                (int32)((Packed >> 10) & 0x3FFu),
                (int32)((Packed >> 20) & 0x3FFu));
            return MeshOrigin + FVector3(LoInt + Q) * GridStep;
        }

        bool TriIntersectsAABB(const FVector3& A, const FVector3& B, const FVector3& C, const FVector3& BMin, const FVector3& BMax)
        {
            const FVector3 TMin = Math::Min(Math::Min(A, B), C);
            const FVector3 TMax = Math::Max(Math::Max(A, B), C);
            return !(TMax.x < BMin.x || TMin.x > BMax.x ||
                     TMax.y < BMin.y || TMin.y > BMax.y ||
                     TMax.z < BMin.z || TMin.z > BMax.z);
        }

        // Tag-bit packed into cache key so one entity may track one collider of each type.
        enum class ENavColliderType : uint8 { Box = 0, Sphere = 1, Mesh = 2, Capsule = 3 };

        FORCEINLINE uint64 PackSourceKey(entt::entity E, ENavColliderType T)
        {
            return ((uint64)(uint32)E << 8) | (uint64)T;
        }

        // Matches Jolt body placement so nav geometry overlaps physics exactly.
        FORCEINLINE FMatrix4 ColliderToWorld(const STransformComponent& X, const FVector3& TransOffset, const FVector3& EulerOffset)
        {
            const FMatrix4 LocalOffset = Math::Translate(FMatrix4(1.0f), TransOffset)
                                        * Math::ToMatrix4(FQuat(EulerOffset));
            return X.GetWorldMatrix() * LocalOffset;
        }

        FORCEINLINE void EmitTri(FGatherAccumulator& Acc, const FVector3& BakeMin, const FVector3& BakeMax, const FVector3& A, const FVector3& B, const FVector3& C)
        {
            if (!TriIntersectsAABB(A, B, C, BakeMin, BakeMax))
            {
                return;
            }
            const uint32 Base = (uint32)Acc.Vertices.size();
            Acc.Vertices.push_back(A);
            Acc.Vertices.push_back(B);
            Acc.Vertices.push_back(C);
            Acc.Indices.push_back(Base + 0);
            Acc.Indices.push_back(Base + 1);
            Acc.Indices.push_back(Base + 2);
            Acc.AABBMin = Math::Min(Acc.AABBMin, Math::Min(A, Math::Min(B, C)));
            Acc.AABBMax = Math::Max(Acc.AABBMax, Math::Max(A, Math::Max(B, C)));
        }

        // 12 tris (2 per face); world matrix precomputed by caller.
        void EmitBoxGeometry(const FMatrix4& W, const FVector3& HalfExtent, const FVector3& BakeMin, const FVector3& BakeMax, FGatherAccumulator& Acc)
        {
            const FVector3 H = HalfExtent;
            const FVector3 LocalCorners[8] = {
                {-H.x,-H.y,-H.z}, { H.x,-H.y,-H.z},
                { H.x,-H.y, H.z}, {-H.x,-H.y, H.z},
                {-H.x, H.y,-H.z}, { H.x, H.y,-H.z},
                { H.x, H.y, H.z}, {-H.x, H.y, H.z},
            };
            FVector3 V[8];
            for (int i = 0; i < 8; ++i)
            {
                V[i] = FVector3(W * FVector4(LocalCorners[i], 1.0f));
            }
            // Outward-wound for Recast's slope test (top face = walkable).
            EmitTri(Acc, BakeMin, BakeMax, V[0], V[1], V[2]); EmitTri(Acc, BakeMin, BakeMax, V[0], V[2], V[3]);
            EmitTri(Acc, BakeMin, BakeMax, V[4], V[6], V[5]); EmitTri(Acc, BakeMin, BakeMax, V[4], V[7], V[6]);
            EmitTri(Acc, BakeMin, BakeMax, V[0], V[5], V[1]); EmitTri(Acc, BakeMin, BakeMax, V[0], V[4], V[5]);
            EmitTri(Acc, BakeMin, BakeMax, V[3], V[2], V[6]); EmitTri(Acc, BakeMin, BakeMax, V[3], V[6], V[7]);
            EmitTri(Acc, BakeMin, BakeMax, V[0], V[7], V[4]); EmitTri(Acc, BakeMin, BakeMax, V[0], V[3], V[7]);
            EmitTri(Acc, BakeMin, BakeMax, V[1], V[6], V[2]); EmitTri(Acc, BakeMin, BakeMax, V[1], V[5], V[6]);
        }

        // Low-poly UV-sphere (12x8); nav doesn't need detail past tile resolution.
        void EmitSphereGeometry(const FMatrix4& W, float Radius, const FVector3& BakeMin, const FVector3& BakeMax, FGatherAccumulator& Acc)
        {
            constexpr int Segments = 12;
            constexpr int Stacks   = 8;
            FVector3 Verts[(Stacks + 1) * (Segments + 1)];
            for (int s = 0; s <= Stacks; ++s)
            {
                const float Phi = LE_PI_F * (float)s / (float)Stacks;
                const float SinP = std::sin(Phi);
                const float CosP = std::cos(Phi);
                for (int g = 0; g <= Segments; ++g)
                {
                    const float Theta = (2.0f * LE_PI_F) * (float)g / (float)Segments;
                    const FVector3 Local(Radius * SinP * std::cos(Theta), Radius * CosP, Radius * SinP * std::sin(Theta));
                    Verts[s * (Segments + 1) + g] = FVector3(W * FVector4(Local, 1.0f));
                }
            }
            for (int s = 0; s < Stacks; ++s)
            {
                for (int g = 0; g < Segments; ++g)
                {
                    const FVector3& A = Verts[(s + 0) * (Segments + 1) + g + 0];
                    const FVector3& B = Verts[(s + 1) * (Segments + 1) + g + 0];
                    const FVector3& C = Verts[(s + 1) * (Segments + 1) + g + 1];
                    const FVector3& D = Verts[(s + 0) * (Segments + 1) + g + 1];
                    EmitTri(Acc, BakeMin, BakeMax, A, D, C);
                    EmitTri(Acc, BakeMin, BakeMax, A, C, B);
                }
            }
        }

        // Cylinder + hemispheres along +Y (Jolt CapsuleShape). Total height = 2*HalfHeight + 2*Radius.
        void EmitCapsuleGeometry(const FMatrix4& W, float HalfHeight, float Radius, const FVector3& BakeMin, const FVector3& BakeMax, FGatherAccumulator& Acc)
        {
            constexpr int Segments = 12;
            constexpr int HemiStacks = 4;
            const int Rings = 2 * HemiStacks + 2;

            TVector<FVector3> Verts;
            Verts.resize(Rings * (Segments + 1));

            int RingIdx = 0;
            // Top hemisphere at +Y * HalfHeight.
            for (int s = 0; s <= HemiStacks; ++s, ++RingIdx)
            {
                const float Phi = (LE_PI_F * 0.5f) * (float)s / (float)HemiStacks;
                const float SinP = std::sin(Phi);
                const float CosP = std::cos(Phi);
                for (int g = 0; g <= Segments; ++g)
                {
                    const float Theta = (2.0f * LE_PI_F) * (float)g / (float)Segments;
                    const FVector3 Local(Radius * SinP * std::cos(Theta), HalfHeight + Radius * CosP, Radius * SinP * std::sin(Theta));
                    Verts[RingIdx * (Segments + 1) + g] = FVector3(W * FVector4(Local, 1.0f));
                }
            }
            // Bottom hemisphere at -Y * HalfHeight.
            for (int s = 1; s <= HemiStacks + 1; ++s, ++RingIdx)
            {
                const float Phi = (LE_PI_F * 0.5f) + (LE_PI_F * 0.5f) * (float)s / (float)(HemiStacks + 1);
                const float SinP = std::sin(Phi);
                const float CosP = std::cos(Phi);
                for (int g = 0; g <= Segments; ++g)
                {
                    const float Theta = (2.0f * LE_PI_F) * (float)g / (float)Segments;
                    const FVector3 Local(Radius * SinP * std::cos(Theta), -HalfHeight + Radius * CosP, Radius * SinP * std::sin(Theta));
                    Verts[RingIdx * (Segments + 1) + g] = FVector3(W * FVector4(Local, 1.0f));
                }
            }

            for (int s = 0; s < Rings - 1; ++s)
            {
                for (int g = 0; g < Segments; ++g)
                {
                    const FVector3& A = Verts[(s + 0) * (Segments + 1) + g + 0];
                    const FVector3& B = Verts[(s + 1) * (Segments + 1) + g + 0];
                    const FVector3& C = Verts[(s + 1) * (Segments + 1) + g + 1];
                    const FVector3& D = Verts[(s + 0) * (Segments + 1) + g + 1];
                    EmitTri(Acc, BakeMin, BakeMax, A, D, C);
                    EmitTri(Acc, BakeMin, BakeMax, A, C, B);
                }
            }
        }

        // Explicit Mesh wins; falls back to StaticMeshComponent. Mirrors Jolt's resolution.
        CStaticMesh* ResolveMeshColliderAsset(const SMeshColliderComponent& MC, const SStaticMeshComponent* Fallback)
        {
            if (CStaticMesh* M = MC.Mesh.Get())
            {
                return M;
            }
            return Fallback ? Fallback->StaticMesh.Get() : nullptr;
        }

        void EmitMeshGeometry(const FMatrix4& W, CStaticMesh* Mesh, const FVector3& BakeMin, const FVector3& BakeMax, FGatherAccumulator& Acc)
        {
            if (!Mesh) return;
            const FMeshResource& Res = Mesh->GetMeshResource();
            const FMeshletData&  Md  = Res.MeshletData;
            if (Md.IsEmpty() || Res.bSkinnedMesh) return;

            const TVector<FMeshletVertex>& MV = Md.MeshletVertices;
            const TVector<uint32>&         MT = Md.MeshletTriangles;

            for (const FGeometrySurface& Surface : Res.GeometrySurfaces)
            {
                const uint32 First = Surface.LODMeshletOffset[0];
                const uint32 Count = Surface.LODMeshletCount[0];
                for (uint32 m = 0; m < Count; ++m)
                {
                    const FMeshlet& Meshlet = Md.Meshlets[First + m];

                    FVector3 LocalVerts[MESHLET_MAX_VERTICES];
                    const uint32 V0 = Meshlet.VertexOffset;
                    for (uint32 v = 0; v < Meshlet.VertexCount; ++v)
                    {
                        const FVector3 Local = DecodePosition(MV[V0 + v].Position, Meshlet.LoInt, Md.MeshOrigin[Meshlet.LODIndex], Md.MeshGridStep[Meshlet.LODIndex]);
                        LocalVerts[v] = FVector3(W * FVector4(Local, 1.0f));
                    }

                    const uint32 T0 = Meshlet.TriangleOffset;
                    for (uint32 t = 0; t < Meshlet.TriangleCount; ++t)
                    {
                        uint32 A, B, C;
                        UnpackTri(MT[T0 + t], A, B, C);
                        EmitTri(Acc, BakeMin, BakeMax, LocalVerts[A], LocalVerts[B], LocalVerts[C]);
                    }
                }
            }
        }

        // Collect nav source colliders from all entities with bAffectsNavigation set.
        void GatherSourceGeometry(const FSystemContext& Context, const FVector3& BakeMin, const FVector3& BakeMax, FGatherAccumulator& Acc)
        {
            auto BoxView = Context.CreateView<SBoxColliderComponent, STransformComponent>();
            for (entt::entity Entity : BoxView)
            {
                SBoxColliderComponent& Box = BoxView.get<SBoxColliderComponent>(Entity);
                if (!Box.bAffectsNavigation) continue;
                const FMatrix4 W = ColliderToWorld(BoxView.get<STransformComponent>(Entity), Box.TranslationOffset, Box.RotationOffset);
                EmitBoxGeometry(W, Box.HalfExtent, BakeMin, BakeMax, Acc);
            }

            auto SphereView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
            for (entt::entity Entity : SphereView)
            {
                SSphereColliderComponent& Sphere = SphereView.get<SSphereColliderComponent>(Entity);
                if (!Sphere.bAffectsNavigation) continue;
                const FMatrix4 W = ColliderToWorld(SphereView.get<STransformComponent>(Entity), Sphere.TranslationOffset, FVector3(0.0f));
                EmitSphereGeometry(W, Sphere.Radius, BakeMin, BakeMax, Acc);
            }

            auto MeshView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
            for (entt::entity Entity : MeshView)
            {
                SMeshColliderComponent& MC = MeshView.get<SMeshColliderComponent>(Entity);
                if (!MC.bAffectsNavigation) continue;
                const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(Entity);
                CStaticMesh* Mesh = ResolveMeshColliderAsset(MC, Fallback);
                const FMatrix4 W = ColliderToWorld(MeshView.get<STransformComponent>(Entity), MC.TranslationOffset, MC.RotationOffset);
                EmitMeshGeometry(W, Mesh, BakeMin, BakeMax, Acc);
            }

            auto CapsuleView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
            for (entt::entity Entity : CapsuleView)
            {
                SCharacterPhysicsComponent& Cap = CapsuleView.get<SCharacterPhysicsComponent>(Entity);
                if (!Cap.bAffectsNavigation) continue;
                const FMatrix4 W = ColliderToWorld(CapsuleView.get<STransformComponent>(Entity), FVector3(0.0f), FVector3(0.0f));
                EmitCapsuleGeometry(W, Cap.HalfHeight, Cap.Radius, BakeMin, BakeMax, Acc);
            }
        }

        FORCEINLINE void TilesForAABB(const FVector3& AABBMin, const FVector3& AABBMax, const FVector3& Origin, float TileWorldSize, int32 TilesX, int32 TilesY,
                                      int32& OutTX0, int32& OutTY0, int32& OutTX1, int32& OutTY1)
        {
            OutTX0 = std::clamp((int32)std::floor((AABBMin.x - Origin.x) / TileWorldSize), 0, TilesX - 1);
            OutTY0 = std::clamp((int32)std::floor((AABBMin.z - Origin.z) / TileWorldSize), 0, TilesY - 1);
            OutTX1 = std::clamp((int32)std::floor((AABBMax.x - Origin.x) / TileWorldSize), 0, TilesX - 1);
            OutTY1 = std::clamp((int32)std::floor((AABBMax.z - Origin.z) / TileWorldSize), 0, TilesY - 1);
        }

        FORCEINLINE uint64 PackTileKey(int32 TX, int32 TY) { return ((uint64)(uint32)TY << 32) | (uint32)TX; }

        // Conservative world AABBs; change detector and cache rebuild MUST produce byte-identical values.

        bool ComputeBoxColliderAABB(const SBoxColliderComponent& Box, const STransformComponent& X, FVector3& OutMin, FVector3& OutMax)
        {
            const FMatrix4 W = ColliderToWorld(X, Box.TranslationOffset, Box.RotationOffset);
            const FVector3 H = Box.HalfExtent;
            const FVector3 LocalCorners[8] = {
                {-H.x,-H.y,-H.z}, { H.x,-H.y,-H.z},
                { H.x,-H.y, H.z}, {-H.x,-H.y, H.z},
                {-H.x, H.y,-H.z}, { H.x, H.y,-H.z},
                { H.x, H.y, H.z}, {-H.x, H.y, H.z},
            };
            OutMin = FVector3( FLT_MAX);
            OutMax = FVector3(-FLT_MAX);
            for (int i = 0; i < 8; ++i)
            {
                const FVector3 P = FVector3(W * FVector4(LocalCorners[i], 1.0f));
                OutMin = Math::Min(OutMin, P);
                OutMax = Math::Max(OutMax, P);
            }
            return true;
        }

        bool ComputeSphereColliderAABB(const SSphereColliderComponent& Sphere, const STransformComponent& X, FVector3& OutMin, FVector3& OutMax)
        {
            const FMatrix4 W = ColliderToWorld(X, Sphere.TranslationOffset, FVector3(0.0f));
            const FVector3 Center = FVector3(W * FVector4(0.0f, 0.0f, 0.0f, 1.0f));
            // Conservative radius: longest column basis under arbitrary scale.
            const float Sx = Math::Length(FVector3(W[0]));
            const float Sy = Math::Length(FVector3(W[1]));
            const float Sz = Math::Length(FVector3(W[2]));
            const float R  = Sphere.Radius * std::max(Sx, std::max(Sy, Sz));
            OutMin = Center - FVector3(R);
            OutMax = Center + FVector3(R);
            return true;
        }

        bool ComputeCapsuleColliderAABB(const SCharacterPhysicsComponent& Cap, const STransformComponent& X, FVector3& OutMin, FVector3& OutMax)
        {
            const FMatrix4 W = X.GetWorldMatrix();
            const FVector3 TopCenter = FVector3(W * FVector4(0.0f,  Cap.HalfHeight, 0.0f, 1.0f));
            const FVector3 BotCenter = FVector3(W * FVector4(0.0f, -Cap.HalfHeight, 0.0f, 1.0f));
            const float Sx = Math::Length(FVector3(W[0]));
            const float Sy = Math::Length(FVector3(W[1]));
            const float Sz = Math::Length(FVector3(W[2]));
            const float R  = Cap.Radius * std::max(Sx, std::max(Sy, Sz));
            OutMin = Math::Min(TopCenter, BotCenter) - FVector3(R);
            OutMax = Math::Max(TopCenter, BotCenter) + FVector3(R);
            return true;
        }

        bool ComputeMeshColliderAABB(const SMeshColliderComponent& MC, const STransformComponent& X, const SStaticMeshComponent* Fallback, FVector3& OutMin, FVector3& OutMax)
        {
            CStaticMesh* Mesh = ResolveMeshColliderAsset(MC, Fallback);
            if (!Mesh || Mesh->GetMeshResource().bSkinnedMesh) return false;

            const FAABB& Local = Mesh->GetAABB();
            const FVector3 Corners[8] = {
                {Local.Min.x, Local.Min.y, Local.Min.z}, {Local.Max.x, Local.Min.y, Local.Min.z},
                {Local.Min.x, Local.Max.y, Local.Min.z}, {Local.Max.x, Local.Max.y, Local.Min.z},
                {Local.Min.x, Local.Min.y, Local.Max.z}, {Local.Max.x, Local.Min.y, Local.Max.z},
                {Local.Min.x, Local.Max.y, Local.Max.z}, {Local.Max.x, Local.Max.y, Local.Max.z},
            };
            const FMatrix4 W = ColliderToWorld(X, MC.TranslationOffset, MC.RotationOffset);
            OutMin = FVector3( FLT_MAX);
            OutMax = FVector3(-FLT_MAX);
            for (int i = 0; i < 8; ++i)
            {
                const FVector3 P = FVector3(W * FVector4(Corners[i], 1.0f));
                OutMin = Math::Min(OutMin, P);
                OutMax = Math::Max(OutMax, P);
            }
            return true;
        }

        // Snapshot AABBs at bake completion so next change-detector tick reports zero diff.
        void RebuildEntityAABBCache(const FSystemContext& Context, THashMap<uint64, FNavSourceEntity>& OutCache)
        {
            OutCache.clear();

            auto BoxView = Context.CreateView<SBoxColliderComponent, STransformComponent>();
            for (entt::entity Entity : BoxView)
            {
                SBoxColliderComponent& Box = BoxView.get<SBoxColliderComponent>(Entity);
                if (!Box.bAffectsNavigation) continue;
                FVector3 Mn, Mx;
                if (!ComputeBoxColliderAABB(Box, BoxView.get<STransformComponent>(Entity), Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Box)] = FNavSourceEntity{ Mn, Mx };
            }

            auto SphereView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
            for (entt::entity Entity : SphereView)
            {
                SSphereColliderComponent& Sphere = SphereView.get<SSphereColliderComponent>(Entity);
                if (!Sphere.bAffectsNavigation) continue;
                FVector3 Mn, Mx;
                if (!ComputeSphereColliderAABB(Sphere, SphereView.get<STransformComponent>(Entity), Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Sphere)] = FNavSourceEntity{ Mn, Mx };
            }

            auto MeshView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
            for (entt::entity Entity : MeshView)
            {
                SMeshColliderComponent& MC = MeshView.get<SMeshColliderComponent>(Entity);
                if (!MC.bAffectsNavigation) continue;
                const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(Entity);
                FVector3 Mn, Mx;
                if (!ComputeMeshColliderAABB(MC, MeshView.get<STransformComponent>(Entity), Fallback, Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Mesh)] = FNavSourceEntity{ Mn, Mx };
            }

            auto CapsuleView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
            for (entt::entity Entity : CapsuleView)
            {
                SCharacterPhysicsComponent& Cap = CapsuleView.get<SCharacterPhysicsComponent>(Entity);
                if (!Cap.bAffectsNavigation) continue;
                FVector3 Mn, Mx;
                if (!ComputeCapsuleColliderAABB(Cap, CapsuleView.get<STransformComponent>(Entity), Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Capsule)] = FNavSourceEntity{ Mn, Mx };
            }
        }

        void FillBuildInput(const FSystemContext& Context, SNavMeshComponent& Comp, FNavBuildInput& Out)
        {
            Out.Settings  = Comp.Settings;
            Out.BoundsMin = Comp.Center - Comp.Extents;
            Out.BoundsMax = Comp.Center + Comp.Extents;

            FGatherAccumulator Acc;
            GatherSourceGeometry(Context, Out.BoundsMin, Out.BoundsMax, Acc);
            Out.Vertices = std::move(Acc.Vertices);
            Out.Indices  = std::move(Acc.Indices);
        }

        void TickComponent(const FSystemContext& Context, entt::entity Entity, SNavMeshComponent& Comp)
        {
            if (Comp.bBakeRequested)
            {
                Comp.bBakeRequested = false;
                SNavMeshSystem::RequestBake(Context, Comp);
            }

            // Drain finished bake; FNavMesh construction is offloaded via PendingInit.
            if (Comp.Runtime.ActiveBake && Comp.Runtime.ActiveBake->bDone.load(std::memory_order_acquire))
            {
                FNavBuildOutput& Out = Comp.Runtime.ActiveBake->Output;

                // Tally non-empty tiles so a zero-walkable bake is loud (usually bounds miss geometry).
                int32 NonEmptyTiles = 0;
                for (const FNavTileData& T : Out.Tiles)
                {
                    if (!T.Blob.empty()) ++NonEmptyTiles;
                }
                if (Out.Tiles.empty() || NonEmptyTiles == 0)
                {
                    LOG_WARN("NavMesh bake produced no walkable tiles ({} tiles total). Verify the bounds volume overlaps source geometry and that meshes are not skinned.", Out.Tiles.size());
                }
                else
                {
                    LOG_INFO("NavMesh bake complete: {}/{} tiles walkable, origin=({:.2f}, {:.2f}, {:.2f}), tileSize={:.2f}.",
                        NonEmptyTiles, (int32)Out.Tiles.size(), Out.Origin.x, Out.Origin.y, Out.Origin.z, Out.TileWorldSize);
                }

                Comp.Tiles           = std::move(Out.Tiles);
                Comp.Origin          = Out.Origin;
                Comp.TileWorldSize   = Out.TileWorldSize;
                Comp.MaxPolysPerTile = Out.MaxPolysPerTile;
                Comp.Runtime.LiveLayout.Origin = Out.Origin;
                Comp.Runtime.LiveLayout.TileWorldSize = Out.TileWorldSize;
                Comp.Runtime.LiveLayout.MaxTiles = Out.MaxTiles;
                Comp.Runtime.LiveLayout.MaxPolysPerTile = Out.MaxPolysPerTile;
                Comp.Runtime.ActiveBake.reset();
                Comp.Runtime.bRuntimeDirty = true;
                RebuildEntityAABBCache(Context, Comp.Runtime.EntityAABBs);
                Comp.Runtime.DirtyTiles.clear();
            }

            // Drain finished async hydration.
            if (Comp.Runtime.PendingInit && Comp.Runtime.PendingInit->bDone.load(std::memory_order_acquire))
            {
                Comp.Runtime.Mesh = std::move(Comp.Runtime.PendingInit->ResultMesh);
                Comp.Runtime.PendingInit.reset();
                // dtNavMesh::init or addTile can fail; without this branch state silently goes Ready.
                if (!Comp.Runtime.Mesh || !Comp.Runtime.Mesh->IsReady())
                {
                    LOG_ERROR("NavMesh hydration failed: dtNavMesh did not initialize (recast vendoring missing, or addTile rejected every blob). All Nav queries will return false.");
                    Comp.Runtime.State = ENavBakeState::Failed;
                    Comp.Runtime.Mesh.reset();
                }
                else
                {
                    Comp.Runtime.State = ENavBakeState::Ready;

                    if (CVarNavDebugLog.GetValue())
                    {
                        const FNavDebugStats Stats = Comp.Runtime.Mesh->GetDebugStats();
                        LOG_INFO("NavMesh ready: {} tiles, {} triangles, {} boundary edges, {} off-mesh links.",
                            Stats.LoadedTiles, Stats.Triangles, Stats.BoundaryEdges, Stats.OffMeshLinks);
                    }
                }
            }

            // Kick async hydration when tiles are present and mesh is missing or dirty.
            // bRuntimeDirty branch is essential: re-bake otherwise leaves old Mesh and state stuck Building.
            if (Comp.HasBakedData() && !Comp.Runtime.PendingInit && (Comp.Runtime.bRuntimeDirty || !Comp.Runtime.Mesh))
            {
                const FVector3 BakeMin = Comp.Center - Comp.Extents;
                const FVector3 BakeMax = Comp.Center + Comp.Extents;
                Comp.Runtime.TilesX = std::max(1, (int32)std::ceil((BakeMax.x - BakeMin.x) / Comp.TileWorldSize));
                Comp.Runtime.TilesY = std::max(1, (int32)std::ceil((BakeMax.z - BakeMin.z) / Comp.TileWorldSize));
                Comp.Runtime.LiveLayout.Origin          = Comp.Origin;
                Comp.Runtime.LiveLayout.TileWorldSize   = Comp.TileWorldSize;
                Comp.Runtime.LiveLayout.MaxTiles        = (int32)Comp.Tiles.size();
                Comp.Runtime.LiveLayout.MaxPolysPerTile = Comp.MaxPolysPerTile;

                auto Job = MakeShared<FNavInitJob>();
                Comp.Runtime.PendingInit = Job;
                Comp.Runtime.bRuntimeDirty = false;
                Comp.Runtime.State = ENavBakeState::Initializing;

                // Copy tiles so worker owns its data; Comp.Tiles stays serialized source of truth.
                TVector<FNavTileData> TilesCopy = Comp.Tiles;
                const FVector3 InitOrigin = Comp.Origin;
                const float InitTileSize = Comp.TileWorldSize;
                const int32 InitMaxTiles = (int32)TilesCopy.size();
                const int32 InitMaxPolys = Comp.MaxPolysPerTile;

                Task::AsyncTask(1, 1, [Job, Tiles = std::move(TilesCopy), InitOrigin, InitTileSize, InitMaxTiles, InitMaxPolys](uint32, uint32, uint32) mutable
                {
                    auto Mesh = MakeUnique<FNavMesh>();
                    Mesh->Initialize(InitOrigin, InitTileSize, InitMaxTiles, InitMaxPolys, std::move(Tiles));
                    Job->ResultMesh = std::move(Mesh);
                    Job->bDone.store(true, std::memory_order_release);
                });

                RebuildEntityAABBCache(Context, Comp.Runtime.EntityAABBs);
                Comp.Runtime.DirtyTiles.clear();
            }

            if (!Comp.Runtime.Mesh || !Comp.Runtime.Mesh->IsReady() || Comp.Runtime.State != ENavBakeState::Ready)
            {
                return;
            }

            // Debug draw runs first so it emits even when later steps early-return.
            if (CVarNavDrawDebug.GetValue())
            {
                DrawNavDebug(Context, Comp);
            }

            // Hot-swap completed per-tile rebakes.
            for (auto& Job : Comp.Runtime.PendingRebakes)
            {
                if (!Job || Job->bConsumed.load(std::memory_order_acquire))
                {
                    continue;
                }
                if (!Job->bDone.load(std::memory_order_acquire))
                {
                    continue;
                }

                // Persist into Comp.Tiles BEFORE the runtime mesh hand-off, or PIE clones/saves init from empty.
                bool bUpdatedExisting = false;
                for (FNavTileData& T : Comp.Tiles)
                {
                    if (T.X == Job->TileX && T.Y == Job->TileY)
                    {
                        T.Blob = Job->ResultBlob;
                        bUpdatedExisting = true;
                        break;
                    }
                }
                if (!bUpdatedExisting)
                {
                    FNavTileData NewTile;
                    NewTile.X = Job->TileX;
                    NewTile.Y = Job->TileY;
                    NewTile.Blob = Job->ResultBlob;
                    Comp.Tiles.push_back(std::move(NewTile));
                }

                Comp.Runtime.Mesh->RebuildTile(Job->TileX, Job->TileY, std::move(Job->ResultBlob));
                Job->bConsumed.store(true, std::memory_order_release);
            }
            
            Comp.Runtime.PendingRebakes.erase(
                eastl::remove_if(Comp.Runtime.PendingRebakes.begin(), Comp.Runtime.PendingRebakes.end(),
                    [](const TSharedPtr<FNavTileRebake>& J) { return !J || J->bConsumed.load(std::memory_order_acquire); }),
                Comp.Runtime.PendingRebakes.end());

            // Detect moved/added/removed source colliders and dirty their tiles.
            THashMap<uint64, FNavSourceEntity> CurrentAABBs;
            CurrentAABBs.reserve(Comp.Runtime.EntityAABBs.size());

            auto MarkDirtyForAABB = [&](const FVector3& Mn, const FVector3& Mx)
            {
                int32 TX0, TY0, TX1, TY1;
                TilesForAABB(Mn, Mx, Comp.Origin, Comp.TileWorldSize, Comp.Runtime.TilesX, Comp.Runtime.TilesY, TX0, TY0, TX1, TY1);
                for (int32 ty = TY0; ty <= TY1; ++ty)
                {
                    for (int32 tx = TX0; tx <= TX1; ++tx)
                    {
                        Comp.Runtime.DirtyTiles.insert(PackTileKey(tx, ty));
                    }
                }
            };

            auto VisitSource = [&](uint64 Key, const FVector3& Mn, const FVector3& Mx)
            {
                CurrentAABBs[Key] = FNavSourceEntity{ Mn, Mx };
                auto It = Comp.Runtime.EntityAABBs.find(Key);
                const bool bNew   = It == Comp.Runtime.EntityAABBs.end();
                const bool bMoved = !bNew && (!Math::IsNearlyEqual(It->second.AABBMin, Mn) || !Math::IsNearlyEqual(It->second.AABBMax, Mx));
                if (bNew || bMoved)
                {
                    if (bMoved)
                    {
                        // Old footprint also dirtied so vacated tris get re-evaluated.
                        MarkDirtyForAABB(It->second.AABBMin, It->second.AABBMax);
                    }
                    MarkDirtyForAABB(Mn, Mx);
                }
            };

            auto BoxCheckView = Context.CreateView<SBoxColliderComponent, STransformComponent>();
            for (entt::entity E : BoxCheckView)
            {
                SBoxColliderComponent& Box = BoxCheckView.get<SBoxColliderComponent>(E);
                if (!Box.bAffectsNavigation) continue;
                FVector3 Mn, Mx;
                if (!ComputeBoxColliderAABB(Box, BoxCheckView.get<STransformComponent>(E), Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Box), Mn, Mx);
            }

            auto SphereCheckView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
            for (entt::entity E : SphereCheckView)
            {
                SSphereColliderComponent& Sphere = SphereCheckView.get<SSphereColliderComponent>(E);
                if (!Sphere.bAffectsNavigation) continue;
                FVector3 Mn, Mx;
                if (!ComputeSphereColliderAABB(Sphere, SphereCheckView.get<STransformComponent>(E), Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Sphere), Mn, Mx);
            }

            auto MeshCheckView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
            for (entt::entity E : MeshCheckView)
            {
                SMeshColliderComponent& MC = MeshCheckView.get<SMeshColliderComponent>(E);
                if (!MC.bAffectsNavigation) continue;
                const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(E);
                FVector3 Mn, Mx;
                if (!ComputeMeshColliderAABB(MC, MeshCheckView.get<STransformComponent>(E), Fallback, Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Mesh), Mn, Mx);
            }

            auto CapCheckView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
            for (entt::entity E : CapCheckView)
            {
                SCharacterPhysicsComponent& Cap = CapCheckView.get<SCharacterPhysicsComponent>(E);
                if (!Cap.bAffectsNavigation) continue;
                FVector3 Mn, Mx;
                if (!ComputeCapsuleColliderAABB(Cap, CapCheckView.get<STransformComponent>(E), Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Capsule), Mn, Mx);
            }

            // Removed colliders dirty their last-known tiles.
            for (const auto& [Id, Snap] : Comp.Runtime.EntityAABBs)
            {
                if (CurrentAABBs.find(Id) == CurrentAABBs.end())
                {
                    MarkDirtyForAABB(Snap.AABBMin, Snap.AABBMax);
                }
            }

            Comp.Runtime.EntityAABBs = std::move(CurrentAABBs);

            // Cap concurrent rebake jobs; remaining dirty tiles wait for next tick.
            constexpr uint32 MaxConcurrent = 8;
            if (Comp.Runtime.DirtyTiles.empty() || Comp.Runtime.PendingRebakes.size() >= MaxConcurrent)
            {
                return;
            }

            // Main thread only snapshots params + world matrices; tessellation runs on a worker.
            const uint32 Capacity = MaxConcurrent - (uint32)Comp.Runtime.PendingRebakes.size();
            TVector<TSharedPtr<FNavTileRebake>> BatchJobs;
            BatchJobs.reserve(Capacity);
            for (auto It = Comp.Runtime.DirtyTiles.begin(); It != Comp.Runtime.DirtyTiles.end() && BatchJobs.size() < Capacity; )
            {
                const uint64 Key = *It;
                It = Comp.Runtime.DirtyTiles.erase(It);

                auto Job = MakeShared<FNavTileRebake>();
                Job->TileX = (int32)(Key & 0xFFFFFFFFu);
                Job->TileY = (int32)(Key >> 32);
                BatchJobs.push_back(Job);
                Comp.Runtime.PendingRebakes.push_back(std::move(Job));
            }

            struct FBoxSnap     { FMatrix4 World; FVector3 HalfExtent; };
            struct FSphereSnap  { FMatrix4 World; float Radius; };
            struct FMeshSnap    { FMatrix4 World; CStaticMesh* Mesh; };
            struct FCapsuleSnap { FMatrix4 World; float HalfHeight; float Radius; };

            struct FInputSnapshot
            {
                TVector<FBoxSnap>     Boxes;
                TVector<FSphereSnap>  Spheres;
                TVector<FMeshSnap>    Meshes;
                TVector<FCapsuleSnap> Capsules;
                FVector3             BakeMin;
                FVector3             BakeMax;
                FNavBuildSettings     Settings;
                FNavBuildOutput       Layout;
            };
            auto Snap = MakeShared<FInputSnapshot>();
            Snap->BakeMin  = Comp.Center - Comp.Extents;
            Snap->BakeMax  = Comp.Center + Comp.Extents;
            Snap->Settings = Comp.Settings;
            Snap->Layout   = Comp.Runtime.LiveLayout;
            {
                auto BoxSnapView = Context.CreateView<SBoxColliderComponent, STransformComponent>();
                for (entt::entity E : BoxSnapView)
                {
                    SBoxColliderComponent& Box = BoxSnapView.get<SBoxColliderComponent>(E);
                    if (!Box.bAffectsNavigation) continue;
                    Snap->Boxes.push_back({ ColliderToWorld(BoxSnapView.get<STransformComponent>(E), Box.TranslationOffset, Box.RotationOffset), Box.HalfExtent });
                }

                auto SphereSnapView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
                for (entt::entity E : SphereSnapView)
                {
                    SSphereColliderComponent& Sphere = SphereSnapView.get<SSphereColliderComponent>(E);
                    if (!Sphere.bAffectsNavigation) continue;
                    Snap->Spheres.push_back({ ColliderToWorld(SphereSnapView.get<STransformComponent>(E), Sphere.TranslationOffset, FVector3(0.0f)), Sphere.Radius });
                }

                auto MeshSnapView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
                for (entt::entity E : MeshSnapView)
                {
                    SMeshColliderComponent& MC = MeshSnapView.get<SMeshColliderComponent>(E);
                    if (!MC.bAffectsNavigation) continue;
                    const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(E);
                    CStaticMesh* M = ResolveMeshColliderAsset(MC, Fallback);
                    if (!M || M->GetMeshResource().bSkinnedMesh) continue;
                    Snap->Meshes.push_back({ ColliderToWorld(MeshSnapView.get<STransformComponent>(E), MC.TranslationOffset, MC.RotationOffset), M });
                }

                auto CapSnapView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
                for (entt::entity E : CapSnapView)
                {
                    SCharacterPhysicsComponent& Cap = CapSnapView.get<SCharacterPhysicsComponent>(E);
                    if (!Cap.bAffectsNavigation) continue;
                    Snap->Capsules.push_back({ ColliderToWorld(CapSnapView.get<STransformComponent>(E), FVector3(0.0f), FVector3(0.0f)), Cap.HalfHeight, Cap.Radius });
                }
            }

            // Coordinator: tessellate once on a worker, ParallelFor the per-tile bakes.
            Task::AsyncTask(1, 1, [Snap, Jobs = std::move(BatchJobs)](uint32, uint32, uint32) mutable
            {
                FNavBuildInput Input;
                Input.BoundsMin = Snap->BakeMin;
                Input.BoundsMax = Snap->BakeMax;
                Input.Settings  = Snap->Settings;

                FGatherAccumulator Acc;
                for (const FBoxSnap& B : Snap->Boxes)
                {
                    EmitBoxGeometry(B.World, B.HalfExtent, Snap->BakeMin, Snap->BakeMax, Acc);
                }
                for (const FSphereSnap& S : Snap->Spheres)
                {
                    EmitSphereGeometry(S.World, S.Radius, Snap->BakeMin, Snap->BakeMax, Acc);
                }
                for (const FMeshSnap& M : Snap->Meshes)
                {
                    EmitMeshGeometry(M.World, M.Mesh, Snap->BakeMin, Snap->BakeMax, Acc);
                }
                for (const FCapsuleSnap& C : Snap->Capsules)
                {
                    EmitCapsuleGeometry(C.World, C.HalfHeight, C.Radius, Snap->BakeMin, Snap->BakeMax, Acc);
                }
                Input.Vertices = std::move(Acc.Vertices);
                Input.Indices  = std::move(Acc.Indices);

                Task::ParallelFor((uint32)Jobs.size(), [&](uint32 i)
                {
                    FNavTileData Out;
                    if (NavMeshBuilder::BakeSingleTile(Input, Snap->Layout, Jobs[i]->TileX, Jobs[i]->TileY, Out))
                    {
                        Jobs[i]->ResultBlob = std::move(Out.Blob);
                    }
                    Jobs[i]->bDone.store(true, std::memory_order_release);
                });
            });

            (void)Entity;
        }

        FNavMesh* FirstReadyNavMesh(const FSystemContext& Context)
        {
            auto View = Context.CreateView<SNavMeshComponent>();
            for (entt::entity Entity : View)
            {
                SNavMeshComponent& Comp = View.get<SNavMeshComponent>(Entity);
                if (Comp.Runtime.Mesh && Comp.Runtime.Mesh->IsReady())
                {
                    return Comp.Runtime.Mesh.get();
                }
            }
            return nullptr;
        }
    }

    namespace
    {
        // Initializes Center from entity transform so bake volume defaults at entity location.
        void OnNavMeshConstructed(entt::registry& Reg, entt::entity Entity)
        {
            SNavMeshComponent& Nav = Reg.get<SNavMeshComponent>(Entity);
            if (auto* Xform = Reg.try_get<STransformComponent>(Entity))
            {
                Nav.Center = Xform->GetWorldTransform().Location;
            }
        }
    }

    void SNavMeshSystem::Startup(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        // entt sinks dedupe on the same function pointer, so reconnect is a no-op.
        Context.GetRegistry().on_construct<SNavMeshComponent>().connect<&OnNavMeshConstructed>();

        auto View = Context.CreateView<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            TickComponent(Context, Entity, View.get<SNavMeshComponent>(Entity));
        }
    }

    void SNavMeshSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = Context.CreateView<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            TickComponent(Context, Entity, View.get<SNavMeshComponent>(Entity));
        }
    }

    void SNavMeshSystem::Teardown(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = Context.CreateView<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            SNavMeshComponent& Comp = View.get<SNavMeshComponent>(Entity);
            if (Comp.Runtime.ActiveBake)
            {
                Comp.Runtime.ActiveBake->bCancelRequested.store(true, std::memory_order_release);
            }
            Comp.Runtime.Mesh.reset();
            Comp.Runtime.ActiveBake.reset();
            // Worker holds its own shared_ptr; clearing here just prevents consumption.
            Comp.Runtime.PendingInit.reset();
            Comp.Runtime.PendingRebakes.clear();
            Comp.Runtime.DirtyTiles.clear();
            Comp.Runtime.EntityAABBs.clear();
            Comp.Runtime.State = ENavBakeState::Idle;
        }
    }

    void SNavMeshSystem::RequestBake(const FSystemContext& Context, SNavMeshComponent& Comp)
    {
        if (Comp.Runtime.ActiveBake)
        {
            LOG_WARN("NavMesh bake requested while one is already in flight. Ignoring (the in-progress bake will complete first).");
            return;
        }

        // Catch zero/negative bounds up front; otherwise user gets a misleading "0 walkable" warning later.
        const FVector3 Span = Comp.Extents * 2.0f;
        if (Span.x <= 0.0f || Span.y <= 0.0f || Span.z <= 0.0f)
        {
            LOG_ERROR("NavMesh bake skipped: bounds extents must be positive on all axes (got {:.2f}, {:.2f}, {:.2f}).",
                Comp.Extents.x, Comp.Extents.y, Comp.Extents.z);
            return;
        }
        if (Comp.Settings.CellSize <= 0.0f || Comp.Settings.CellHeight <= 0.0f || Comp.Settings.TileSizeVoxels <= 0)
        {
            LOG_ERROR("NavMesh bake skipped: invalid voxel settings (CellSize={:.3f}, CellHeight={:.3f}, TileSizeVoxels={}).",
                Comp.Settings.CellSize, Comp.Settings.CellHeight, Comp.Settings.TileSizeVoxels);
            return;
        }

        FNavBuildInput Input;
        FillBuildInput(Context, Comp, Input);

        if (Input.Vertices.empty() || Input.Indices.empty())
        {
            LOG_WARN("NavMesh bake starting with no source geometry inside bounds. The result will be an empty navmesh; check that static meshes (non-character-controller) overlap the bounds volume.");
        }
        else
        {
            LOG_INFO("NavMesh bake starting: {} verts, {} tris, bounds=({:.1f},{:.1f},{:.1f})..({:.1f},{:.1f},{:.1f}).",
                (int32)Input.Vertices.size(), (int32)(Input.Indices.size() / 3),
                Input.BoundsMin.x, Input.BoundsMin.y, Input.BoundsMin.z,
                Input.BoundsMax.x, Input.BoundsMax.y, Input.BoundsMax.z);
        }

        // EntityAABB cache populated at bake-completion drain (avoids tight-vs-conservative AABB mismatch storm).
        Comp.Runtime.EntityAABBs.clear();
        Comp.Runtime.DirtyTiles.clear();
        Comp.Runtime.PendingRebakes.clear();
        Comp.Runtime.ActiveBake = NavMeshBuilder::Bake(std::move(Input));
        Comp.Runtime.State = ENavBakeState::Building;
    }

    namespace Nav
    {
        bool FindPath(const FSystemContext& Context, const FVector3& Start, const FVector3& End, const FNavQueryFilter& Filter, FNavPath& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->FindPath(Start, End, Filter, Out);
        }

        bool ProjectPoint(const FSystemContext& Context, const FVector3& World, const FVector3& Extents, const FNavQueryFilter& Filter, FVector3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->ProjectPoint(World, Extents, Filter, Out);
        }

        bool Raycast(const FSystemContext& Context, const FVector3& Start, const FVector3& End, const FNavQueryFilter& Filter, FVector3& HitOut)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->Raycast(Start, End, Filter, HitOut);
        }

        namespace
        {
            FNavMesh* FirstReadyNavMeshFromWorld(CWorld* World)
            {
                if (!World) return nullptr;
                auto& Reg = World->GetEntityRegistry();
                auto View = Reg.view<SNavMeshComponent>();
                for (entt::entity E : View)
                {
                    SNavMeshComponent& Comp = View.get<SNavMeshComponent>(E);
                    if (Comp.Runtime.Mesh && Comp.Runtime.Mesh->IsReady())
                    {
                        return Comp.Runtime.Mesh.get();
                    }
                }
                return nullptr;
            }
        }

        bool IsReady(CWorld* World)
        {
            return FirstReadyNavMeshFromWorld(World) != nullptr;
        }

        bool FindPath(CWorld* World, const FVector3& Start, const FVector3& End, FNavPath& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->FindPath(Start, End, Filter, Out);
        }

        bool ProjectPoint(CWorld* World, const FVector3& Point, const FVector3& Extents, FVector3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->ProjectPoint(Point, Extents, Filter, Out);
        }

        bool Raycast(CWorld* World, const FVector3& Start, const FVector3& End, FVector3& OutHit)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->Raycast(Start, End, Filter, OutHit);
        }

        bool FindRandomReachablePoint(CWorld* World, const FVector3& Origin, float Radius, FVector3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->FindRandomPoint(Origin, Radius, Filter, Out);
        }

        bool IsReachable(CWorld* World, const FVector3& From, const FVector3& To)
        {
            FNavPath Path;
            return FindPath(World, From, To, Path) && Path.bValid && !Path.bPartial;
        }

        float PathLength(CWorld* World, const FVector3& From, const FVector3& To)
        {
            FNavPath Path;
            if (!FindPath(World, From, To, Path) || !Path.bValid) return -1.0f;
            float Len = 0.0f;
            for (size_t i = 1; i < Path.Corners.size(); ++i)
            {
                Len += Math::Length(Path.Corners[i] - Path.Corners[i - 1]);
            }
            return Len;
        }

        void DrawPath(CWorld* World, const FNavPath& Path, const FVector4& Color, float Thickness, float Lift, float Duration)
        {
            if (!World || !Path.bValid || Path.Corners.size() < 2) return;

            const FVector3 LiftV(0.0f, Lift, 0.0f);
            for (size_t i = 1; i < Path.Corners.size(); ++i)
            {
                World->DrawLine(Path.Corners[i - 1] + LiftV, Path.Corners[i] + LiftV, Color, Thickness, true, Duration);
            }
            // Corner spheres so kinks read at a glance; goal sphere distinguished.
            const float R = 0.12f;
            for (size_t i = 0; i < Path.Corners.size(); ++i)
            {
                const bool bGoal = (i + 1 == Path.Corners.size());
                const FVector4 SphColor = bGoal ? FVector4(1.0f, 1.0f, 1.0f, 1.0f) : Color;
                World->DrawSphere(Path.Corners[i] + LiftV, bGoal ? R * 1.6f : R, SphColor, 10, 1.0f, true, Duration);
            }
            // Partial paths are easy to misread; flag them with a red marker on the last corner.
            if (Path.bPartial)
            {
                World->DrawSphere(Path.Corners.back() + LiftV, 0.25f, FVector4(1.0f, 0.2f, 0.2f, 1.0f), 12, 1.0f, true, Duration);
            }
        }

        bool DrawDebugPath(CWorld* World, const FVector3& From, const FVector3& To, const FVector4& Color, float Duration)
        {
            FNavPath Path;
            if (!FindPath(World, From, To, Path) || !Path.bValid) return false;
            DrawPath(World, Path, Color, 3.0f, 0.15f, Duration);
            return true;
        }

        void RegisterLuaModule(Lua::FRef& Globals)
        {
            Lua::FRef NavTable = Globals.NewTable("Nav");

            NavTable.SetFunction<[](CWorld* W) { return Nav::IsReady(W); }>("IsReady");
            NavTable.SetFunction<[](CWorld* W, FVector3 From, FVector3 To) { return Nav::IsReachable(W, From, To); }>("IsReachable");

            // PathLength returns < 0 when no path.
            NavTable.SetFunction<[](CWorld* W, FVector3 From, FVector3 To) { return Nav::PathLength(W, From, To); }>("PathLength");

            // Returns the input on failure; pair with IsReady/IsReachable when validity matters.
            NavTable.SetFunction<[](CWorld* W, FVector3 P, FVector3 E) -> FVector3
            {
                FVector3 Out = P;
                Nav::ProjectPoint(W, P, E, Out);
                return Out;
            }>("ProjectPoint");

            NavTable.SetFunction<[](CWorld* W, FVector3 S, FVector3 E) -> FVector3
            {
                FVector3 Out = E;
                Nav::Raycast(W, S, E, Out);
                return Out;
            }>("Raycast");

            NavTable.SetFunction<[](CWorld* W, FVector3 O, float R) -> FVector3
            {
                FVector3 Out = O;
                Nav::FindRandomReachablePoint(W, O, R, Out);
                return Out;
            }>("FindRandomReachablePoint");

            // Empty array when no path exists.
            NavTable.SetFunction<[](CWorld* W, FVector3 S, FVector3 E) -> TVector<FVector3>
            {
                FNavPath Path;
                Nav::FindPath(W, S, E, Path);
                return Path.Corners;
            }>("FindPath");

            // Duration <= 0 draws for a single frame; useful for tick-driven scripts.
            NavTable.SetFunction<[](CWorld* W, FVector3 S, FVector3 E, float Duration) -> bool
            {
                return Nav::DrawDebugPath(W, S, E, FVector4(0.10f, 1.0f, 0.95f, 1.0f), Duration);
            }>("DrawDebugPath");
        }
    }
}
