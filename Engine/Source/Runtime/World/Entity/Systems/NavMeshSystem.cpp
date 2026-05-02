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
    static TConsoleVar<bool> CVarNavDrawDebug(
        "Nav.DrawDebug",
        false,
        "When true, every nav-mesh component emits debug lines for its walkable triangles each tick.");

    namespace
    {
        struct FGatherAccumulator
        {
            TVector<glm::vec3> Vertices;
            TVector<uint32>    Indices;
            glm::vec3          AABBMin = glm::vec3( FLT_MAX);
            glm::vec3          AABBMax = glm::vec3(-FLT_MAX);
        };

        // Triangle indices in MeshletTriangles are 8-bit local indices packed
        // 3 per uint32. Each meshlet's vertex indices are local to its
        // VertexOffset in MeshletVertices.
        FORCEINLINE void UnpackTri(uint32 Packed, uint32& A, uint32& B, uint32& C)
        {
            A = (Packed >> 0)  & 0xFFu;
            B = (Packed >> 8)  & 0xFFu;
            C = (Packed >> 16) & 0xFFu;
        }

        // Position is 10-10-10 unsigned. Decode is MeshOrigin + (LoInt + q) * GridStep.
        FORCEINLINE glm::vec3 DecodePosition(uint32 Packed, const glm::ivec3& LoInt, const glm::vec3& MeshOrigin, const glm::vec3& GridStep)
        {
            const glm::ivec3 Q(
                (int32)((Packed >>  0) & 0x3FFu),
                (int32)((Packed >> 10) & 0x3FFu),
                (int32)((Packed >> 20) & 0x3FFu));
            return MeshOrigin + glm::vec3(LoInt + Q) * GridStep;
        }

        bool TriIntersectsAABB(const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, const glm::vec3& BMin, const glm::vec3& BMax)
        {
            const glm::vec3 TMin = glm::min(glm::min(A, B), C);
            const glm::vec3 TMax = glm::max(glm::max(A, B), C);
            return !(TMax.x < BMin.x || TMin.x > BMax.x ||
                     TMax.y < BMin.y || TMin.y > BMax.y ||
                     TMax.z < BMin.z || TMin.z > BMax.z);
        }

        // Tag-bit packed into the cache key so a single entity may carry one
        // collider of each type tracked independently for change detection.
        enum class ENavColliderType : uint8 { Box = 0, Sphere = 1, Mesh = 2, Capsule = 3 };

        FORCEINLINE uint64 PackSourceKey(entt::entity E, ENavColliderType T)
        {
            return ((uint64)(uint32)E << 8) | (uint64)T;
        }

        // Build the world-space matrix for a collider given its entity
        // transform and local offset / euler-rotation. Matches the matrix
        // composition used by Jolt body placement so nav geometry overlaps
        // physics geometry exactly.
        FORCEINLINE glm::mat4 ColliderToWorld(const STransformComponent& X, const glm::vec3& TransOffset, const glm::vec3& EulerOffset)
        {
            const glm::mat4 LocalOffset = glm::translate(glm::mat4(1.0f), TransOffset)
                                        * glm::mat4_cast(glm::quat(EulerOffset));
            return X.GetWorldMatrix() * LocalOffset;
        }

        FORCEINLINE void EmitTri(FGatherAccumulator& Acc, const glm::vec3& BakeMin, const glm::vec3& BakeMax, const glm::vec3& A, const glm::vec3& B, const glm::vec3& C)
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
            Acc.AABBMin = glm::min(Acc.AABBMin, glm::min(A, glm::min(B, C)));
            Acc.AABBMax = glm::max(Acc.AABBMax, glm::max(A, glm::max(B, C)));
        }

        // Box: 12 triangles (2 per face). World matrix is precomputed by the
        // caller (entity transform composed with the collider's local offset)
        // so the same lower-tier emit can serve both the main-thread gather
        // and the worker snapshot path.
        void EmitBoxGeometry(const glm::mat4& W, const glm::vec3& HalfExtent, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc)
        {
            const glm::vec3 H = HalfExtent;
            const glm::vec3 LocalCorners[8] = {
                {-H.x,-H.y,-H.z}, { H.x,-H.y,-H.z},
                { H.x,-H.y, H.z}, {-H.x,-H.y, H.z},
                {-H.x, H.y,-H.z}, { H.x, H.y,-H.z},
                { H.x, H.y, H.z}, {-H.x, H.y, H.z},
            };
            glm::vec3 V[8];
            for (int i = 0; i < 8; ++i)
            {
                V[i] = glm::vec3(W * glm::vec4(LocalCorners[i], 1.0f));
            }
            // Faces: -Y, +Y, -Z, +Z, -X, +X. Wound so (v1-v0) × (v2-v0)
            // points OUTWARD, which is what Recast's slope test expects -
            // a top face with an upward normal is the one marked walkable.
            EmitTri(Acc, BakeMin, BakeMax, V[0], V[1], V[2]); EmitTri(Acc, BakeMin, BakeMax, V[0], V[2], V[3]);
            EmitTri(Acc, BakeMin, BakeMax, V[4], V[6], V[5]); EmitTri(Acc, BakeMin, BakeMax, V[4], V[7], V[6]);
            EmitTri(Acc, BakeMin, BakeMax, V[0], V[5], V[1]); EmitTri(Acc, BakeMin, BakeMax, V[0], V[4], V[5]);
            EmitTri(Acc, BakeMin, BakeMax, V[3], V[2], V[6]); EmitTri(Acc, BakeMin, BakeMax, V[3], V[6], V[7]);
            EmitTri(Acc, BakeMin, BakeMax, V[0], V[7], V[4]); EmitTri(Acc, BakeMin, BakeMax, V[0], V[3], V[7]);
            EmitTri(Acc, BakeMin, BakeMax, V[1], V[6], V[2]); EmitTri(Acc, BakeMin, BakeMax, V[1], V[5], V[6]);
        }

        // Sphere: low-poly UV-sphere (12 segments x 8 stacks = 192 tris).
        // Cheap to bake into; nav doesn't need detail past tile resolution.
        void EmitSphereGeometry(const glm::mat4& W, float Radius, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc)
        {
            constexpr int Segments = 12;
            constexpr int Stacks   = 8;
            glm::vec3 Verts[(Stacks + 1) * (Segments + 1)];
            for (int s = 0; s <= Stacks; ++s)
            {
                const float Phi = LE_PI_F * (float)s / (float)Stacks;
                const float SinP = std::sin(Phi);
                const float CosP = std::cos(Phi);
                for (int g = 0; g <= Segments; ++g)
                {
                    const float Theta = (2.0f * LE_PI_F) * (float)g / (float)Segments;
                    const glm::vec3 Local(Radius * SinP * std::cos(Theta), Radius * CosP, Radius * SinP * std::sin(Theta));
                    Verts[s * (Segments + 1) + g] = glm::vec3(W * glm::vec4(Local, 1.0f));
                }
            }
            for (int s = 0; s < Stacks; ++s)
            {
                for (int g = 0; g < Segments; ++g)
                {
                    const glm::vec3& A = Verts[(s + 0) * (Segments + 1) + g + 0];
                    const glm::vec3& B = Verts[(s + 1) * (Segments + 1) + g + 0];
                    const glm::vec3& C = Verts[(s + 1) * (Segments + 1) + g + 1];
                    const glm::vec3& D = Verts[(s + 0) * (Segments + 1) + g + 1];
                    // Outward winding: A→D→C and A→C→B keep normals radial.
                    EmitTri(Acc, BakeMin, BakeMax, A, D, C);
                    EmitTri(Acc, BakeMin, BakeMax, A, C, B);
                }
            }
        }

        // Capsule: cylindrical side band + two hemispheres along the +Y
        // axis (Jolt's CapsuleShape convention). Total height is
        // 2*HalfHeight + 2*Radius. Lower-tier so the worker snapshot path
        // can call it with a precomputed world matrix.
        void EmitCapsuleGeometry(const glm::mat4& W, float HalfHeight, float Radius, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc)
        {
            constexpr int Segments = 12;
            constexpr int HemiStacks = 4;
            const int Rings = 2 * HemiStacks + 2; // top hemi + cylinder seam + bottom hemi

            TVector<glm::vec3> Verts;
            Verts.resize(Rings * (Segments + 1));

            int RingIdx = 0;
            // Top hemisphere (Phi 0..pi/2), centered at +Y * HalfHeight.
            for (int s = 0; s <= HemiStacks; ++s, ++RingIdx)
            {
                const float Phi = (LE_PI_F * 0.5f) * (float)s / (float)HemiStacks;
                const float SinP = std::sin(Phi);
                const float CosP = std::cos(Phi);
                for (int g = 0; g <= Segments; ++g)
                {
                    const float Theta = (2.0f * LE_PI_F) * (float)g / (float)Segments;
                    const glm::vec3 Local(Radius * SinP * std::cos(Theta), HalfHeight + Radius * CosP, Radius * SinP * std::sin(Theta));
                    Verts[RingIdx * (Segments + 1) + g] = glm::vec3(W * glm::vec4(Local, 1.0f));
                }
            }
            // Bottom hemisphere (Phi pi/2..pi), centered at -Y * HalfHeight.
            for (int s = 1; s <= HemiStacks + 1; ++s, ++RingIdx)
            {
                const float Phi = (LE_PI_F * 0.5f) + (LE_PI_F * 0.5f) * (float)s / (float)(HemiStacks + 1);
                const float SinP = std::sin(Phi);
                const float CosP = std::cos(Phi);
                for (int g = 0; g <= Segments; ++g)
                {
                    const float Theta = (2.0f * LE_PI_F) * (float)g / (float)Segments;
                    const glm::vec3 Local(Radius * SinP * std::cos(Theta), -HalfHeight + Radius * CosP, Radius * SinP * std::sin(Theta));
                    Verts[RingIdx * (Segments + 1) + g] = glm::vec3(W * glm::vec4(Local, 1.0f));
                }
            }

            for (int s = 0; s < Rings - 1; ++s)
            {
                for (int g = 0; g < Segments; ++g)
                {
                    const glm::vec3& A = Verts[(s + 0) * (Segments + 1) + g + 0];
                    const glm::vec3& B = Verts[(s + 1) * (Segments + 1) + g + 0];
                    const glm::vec3& C = Verts[(s + 1) * (Segments + 1) + g + 1];
                    const glm::vec3& D = Verts[(s + 0) * (Segments + 1) + g + 1];
                    EmitTri(Acc, BakeMin, BakeMax, A, D, C);
                    EmitTri(Acc, BakeMin, BakeMax, A, C, B);
                }
            }
        }

        // Resolve the mesh asset for a SMeshColliderComponent: explicit Mesh
        // wins, otherwise fall back to the entity's StaticMeshComponent.
        // Mirrors the resolution Jolt uses to build collider shapes.
        CStaticMesh* ResolveMeshColliderAsset(const SMeshColliderComponent& MC, const SStaticMeshComponent* Fallback)
        {
            if (CStaticMesh* M = MC.Mesh.Get())
            {
                return M;
            }
            return Fallback ? Fallback->StaticMesh.Get() : nullptr;
        }

        void EmitMeshGeometry(const glm::mat4& W, CStaticMesh* Mesh, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc)
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

                    glm::vec3 LocalVerts[MESHLET_MAX_VERTICES];
                    const uint32 V0 = Meshlet.VertexOffset;
                    for (uint32 v = 0; v < Meshlet.VertexCount; ++v)
                    {
                        const glm::vec3 Local = DecodePosition(MV[V0 + v].Position, Meshlet.LoInt, Md.MeshOrigin, Md.MeshGridStep);
                        LocalVerts[v] = glm::vec3(W * glm::vec4(Local, 1.0f));
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

        // One pass over the registry collecting nav source colliders from
        // every entity that opted in via bAffectsNavigation. Character
        // capsules participate too when their bAffectsNavigation flag is on,
        // so other agents path around them.
        void GatherSourceGeometry(const FSystemContext& Context, const glm::vec3& BakeMin, const glm::vec3& BakeMax, FGatherAccumulator& Acc)
        {
            auto BoxView = Context.CreateView<SBoxColliderComponent, STransformComponent>();
            for (entt::entity Entity : BoxView)
            {
                SBoxColliderComponent& Box = BoxView.get<SBoxColliderComponent>(Entity);
                if (!Box.bAffectsNavigation) continue;
                const glm::mat4 W = ColliderToWorld(BoxView.get<STransformComponent>(Entity), Box.TranslationOffset, Box.RotationOffset);
                EmitBoxGeometry(W, Box.HalfExtent, BakeMin, BakeMax, Acc);
            }

            auto SphereView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
            for (entt::entity Entity : SphereView)
            {
                SSphereColliderComponent& Sphere = SphereView.get<SSphereColliderComponent>(Entity);
                if (!Sphere.bAffectsNavigation) continue;
                const glm::mat4 W = ColliderToWorld(SphereView.get<STransformComponent>(Entity), Sphere.TranslationOffset, glm::vec3(0.0f));
                EmitSphereGeometry(W, Sphere.Radius, BakeMin, BakeMax, Acc);
            }

            auto MeshView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
            for (entt::entity Entity : MeshView)
            {
                SMeshColliderComponent& MC = MeshView.get<SMeshColliderComponent>(Entity);
                if (!MC.bAffectsNavigation) continue;
                const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(Entity);
                CStaticMesh* Mesh = ResolveMeshColliderAsset(MC, Fallback);
                const glm::mat4 W = ColliderToWorld(MeshView.get<STransformComponent>(Entity), MC.TranslationOffset, MC.RotationOffset);
                EmitMeshGeometry(W, Mesh, BakeMin, BakeMax, Acc);
            }

            auto CapsuleView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
            for (entt::entity Entity : CapsuleView)
            {
                SCharacterPhysicsComponent& Cap = CapsuleView.get<SCharacterPhysicsComponent>(Entity);
                if (!Cap.bAffectsNavigation) continue;
                const glm::mat4 W = ColliderToWorld(CapsuleView.get<STransformComponent>(Entity), glm::vec3(0.0f), glm::vec3(0.0f));
                EmitCapsuleGeometry(W, Cap.HalfHeight, Cap.Radius, BakeMin, BakeMax, Acc);
            }
        }

        // Compute the inclusive (TX,TY) range of tiles covered by a world AABB.
        FORCEINLINE void TilesForAABB(const glm::vec3& AABBMin, const glm::vec3& AABBMax, const glm::vec3& Origin, float TileWorldSize, int32 TilesX, int32 TilesY,
                                      int32& OutTX0, int32& OutTY0, int32& OutTX1, int32& OutTY1)
        {
            OutTX0 = std::clamp((int32)std::floor((AABBMin.x - Origin.x) / TileWorldSize), 0, TilesX - 1);
            OutTY0 = std::clamp((int32)std::floor((AABBMin.z - Origin.z) / TileWorldSize), 0, TilesY - 1);
            OutTX1 = std::clamp((int32)std::floor((AABBMax.x - Origin.x) / TileWorldSize), 0, TilesX - 1);
            OutTY1 = std::clamp((int32)std::floor((AABBMax.z - Origin.z) / TileWorldSize), 0, TilesY - 1);
        }

        FORCEINLINE uint64 PackTileKey(int32 TX, int32 TY) { return ((uint64)(uint32)TY << 32) | (uint32)TX; }

        // Conservative world-space AABBs for each collider type. Used by
        // the change detector and the bake-completion cache rebuild; both
        // call sites must produce byte-identical values for change
        // detection to report zero diff after a fresh bake.

        bool ComputeBoxColliderAABB(const SBoxColliderComponent& Box, const STransformComponent& X, glm::vec3& OutMin, glm::vec3& OutMax)
        {
            const glm::mat4 W = ColliderToWorld(X, Box.TranslationOffset, Box.RotationOffset);
            const glm::vec3 H = Box.HalfExtent;
            const glm::vec3 LocalCorners[8] = {
                {-H.x,-H.y,-H.z}, { H.x,-H.y,-H.z},
                { H.x,-H.y, H.z}, {-H.x,-H.y, H.z},
                {-H.x, H.y,-H.z}, { H.x, H.y,-H.z},
                { H.x, H.y, H.z}, {-H.x, H.y, H.z},
            };
            OutMin = glm::vec3( FLT_MAX);
            OutMax = glm::vec3(-FLT_MAX);
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 P = glm::vec3(W * glm::vec4(LocalCorners[i], 1.0f));
                OutMin = glm::min(OutMin, P);
                OutMax = glm::max(OutMax, P);
            }
            return true;
        }

        bool ComputeSphereColliderAABB(const SSphereColliderComponent& Sphere, const STransformComponent& X, glm::vec3& OutMin, glm::vec3& OutMax)
        {
            const glm::mat4 W = ColliderToWorld(X, Sphere.TranslationOffset, glm::vec3(0.0f));
            const glm::vec3 Center = glm::vec3(W * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            // Conservative radius under arbitrary scale: longest column basis.
            const float Sx = glm::length(glm::vec3(W[0]));
            const float Sy = glm::length(glm::vec3(W[1]));
            const float Sz = glm::length(glm::vec3(W[2]));
            const float R  = Sphere.Radius * std::max(Sx, std::max(Sy, Sz));
            OutMin = Center - glm::vec3(R);
            OutMax = Center + glm::vec3(R);
            return true;
        }

        bool ComputeCapsuleColliderAABB(const SCharacterPhysicsComponent& Cap, const STransformComponent& X, glm::vec3& OutMin, glm::vec3& OutMax)
        {
            const glm::mat4 W = X.GetWorldMatrix();
            const glm::vec3 TopCenter = glm::vec3(W * glm::vec4(0.0f,  Cap.HalfHeight, 0.0f, 1.0f));
            const glm::vec3 BotCenter = glm::vec3(W * glm::vec4(0.0f, -Cap.HalfHeight, 0.0f, 1.0f));
            const float Sx = glm::length(glm::vec3(W[0]));
            const float Sy = glm::length(glm::vec3(W[1]));
            const float Sz = glm::length(glm::vec3(W[2]));
            const float R  = Cap.Radius * std::max(Sx, std::max(Sy, Sz));
            OutMin = glm::min(TopCenter, BotCenter) - glm::vec3(R);
            OutMax = glm::max(TopCenter, BotCenter) + glm::vec3(R);
            return true;
        }

        bool ComputeMeshColliderAABB(const SMeshColliderComponent& MC, const STransformComponent& X, const SStaticMeshComponent* Fallback, glm::vec3& OutMin, glm::vec3& OutMax)
        {
            CStaticMesh* Mesh = ResolveMeshColliderAsset(MC, Fallback);
            if (!Mesh || Mesh->GetMeshResource().bSkinnedMesh) return false;

            const FAABB& Local = Mesh->GetAABB();
            const glm::vec3 Corners[8] = {
                {Local.Min.x, Local.Min.y, Local.Min.z}, {Local.Max.x, Local.Min.y, Local.Min.z},
                {Local.Min.x, Local.Max.y, Local.Min.z}, {Local.Max.x, Local.Max.y, Local.Min.z},
                {Local.Min.x, Local.Min.y, Local.Max.z}, {Local.Max.x, Local.Min.y, Local.Max.z},
                {Local.Min.x, Local.Max.y, Local.Max.z}, {Local.Max.x, Local.Max.y, Local.Max.z},
            };
            const glm::mat4 W = ColliderToWorld(X, MC.TranslationOffset, MC.RotationOffset);
            OutMin = glm::vec3( FLT_MAX);
            OutMax = glm::vec3(-FLT_MAX);
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 P = glm::vec3(W * glm::vec4(Corners[i], 1.0f));
                OutMin = glm::min(OutMin, P);
                OutMax = glm::max(OutMax, P);
            }
            return true;
        }

        // Snapshot every nav-source collider's conservative AABB into the
        // cache. Called at bake completion so the next change-detector tick
        // compares apples to apples and reports zero diff.
        void RebuildEntityAABBCache(const FSystemContext& Context, THashMap<uint64, FNavSourceEntity>& OutCache)
        {
            OutCache.clear();

            auto BoxView = Context.CreateView<SBoxColliderComponent, STransformComponent>();
            for (entt::entity Entity : BoxView)
            {
                SBoxColliderComponent& Box = BoxView.get<SBoxColliderComponent>(Entity);
                if (!Box.bAffectsNavigation) continue;
                glm::vec3 Mn, Mx;
                if (!ComputeBoxColliderAABB(Box, BoxView.get<STransformComponent>(Entity), Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Box)] = FNavSourceEntity{ Mn, Mx };
            }

            auto SphereView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
            for (entt::entity Entity : SphereView)
            {
                SSphereColliderComponent& Sphere = SphereView.get<SSphereColliderComponent>(Entity);
                if (!Sphere.bAffectsNavigation) continue;
                glm::vec3 Mn, Mx;
                if (!ComputeSphereColliderAABB(Sphere, SphereView.get<STransformComponent>(Entity), Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Sphere)] = FNavSourceEntity{ Mn, Mx };
            }

            auto MeshView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
            for (entt::entity Entity : MeshView)
            {
                SMeshColliderComponent& MC = MeshView.get<SMeshColliderComponent>(Entity);
                if (!MC.bAffectsNavigation) continue;
                const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(Entity);
                glm::vec3 Mn, Mx;
                if (!ComputeMeshColliderAABB(MC, MeshView.get<STransformComponent>(Entity), Fallback, Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Mesh)] = FNavSourceEntity{ Mn, Mx };
            }

            auto CapsuleView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
            for (entt::entity Entity : CapsuleView)
            {
                SCharacterPhysicsComponent& Cap = CapsuleView.get<SCharacterPhysicsComponent>(Entity);
                if (!Cap.bAffectsNavigation) continue;
                glm::vec3 Mn, Mx;
                if (!ComputeCapsuleColliderAABB(Cap, CapsuleView.get<STransformComponent>(Entity), Mn, Mx)) continue;
                OutCache[PackSourceKey(Entity, ENavColliderType::Capsule)] = FNavSourceEntity{ Mn, Mx };
            }
        }

        // Build the full FNavBuildInput consumed by Bake() / BakeSingleTile().
        // Lazy-evaluated by the caller and reused for both initial bake and
        // partial rebuilds.
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

        // Walk components, finalize completed bakes, drain per-tile rebakes,
        // detect entity AABB changes and dirty the affected tiles, and kick
        // any pending rebake jobs.
        void TickComponent(const FSystemContext& Context, entt::entity Entity, SNavMeshComponent& Comp)
        {
            // 0. Editor / gameplay requested a fresh bake. Convert the flag
            //    into a real RequestBake call now that we have a context.
            if (Comp.bBakeRequested)
            {
                Comp.bBakeRequested = false;
                SNavMeshSystem::RequestBake(Context, Comp);
            }

            // 1. Drain a finished full bake. We don't construct the FNavMesh
            // here - that work goes onto a worker via PendingInit so main
            // never pays the dtNavMesh::init + per-tile addTile cost.
            if (Comp.Runtime.ActiveBake && Comp.Runtime.ActiveBake->bDone.load(std::memory_order_acquire))
            {
                FNavBuildOutput& Out = Comp.Runtime.ActiveBake->Output;

                // Tally non-empty tiles so a bake that produced *zero*
                // walkable surface is loud instead of silent. The most
                // common cause is the bounds volume not actually
                // overlapping any static-mesh geometry.
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

            // 2. Drain a finished async hydration. The worker built the
            // FNavMesh + triangle cache; main thread just hands it over.
            if (Comp.Runtime.PendingInit && Comp.Runtime.PendingInit->bDone.load(std::memory_order_acquire))
            {
                Comp.Runtime.Mesh = std::move(Comp.Runtime.PendingInit->ResultMesh);
                Comp.Runtime.PendingInit.reset();
                // dtNavMesh::init or the per-tile addTile loop can fail
                // and leave the FNavMesh non-ready. Without this branch
                // the system silently transitions to Ready and every
                // pathfinding query no-ops with no log trail.
                if (!Comp.Runtime.Mesh || !Comp.Runtime.Mesh->IsReady())
                {
                    LOG_ERROR("NavMesh hydration failed: dtNavMesh did not initialize (recast vendoring missing, or addTile rejected every blob). All Nav queries will return false.");
                    Comp.Runtime.State = ENavBakeState::Failed;
                    Comp.Runtime.Mesh.reset();
                }
                else
                {
                    Comp.Runtime.State = ENavBakeState::Ready;
                }
            }

            // 3. Kick async hydration when tiles are present and either we
            // have no live mesh yet (first bake / PIE clone / world load)
            // OR the tiles changed since the last init (subsequent bakes
            // set bRuntimeDirty in step 1). Without the bRuntimeDirty
            // branch, a re-bake leaves the old Mesh in place, the
            // condition stays false, and the editor stays stuck on
            // "Baking..." because State never leaves Building.
            if (Comp.HasBakedData() && !Comp.Runtime.PendingInit && (Comp.Runtime.bRuntimeDirty || !Comp.Runtime.Mesh))
            {
                const glm::vec3 BakeMin = Comp.Center - Comp.Extents;
                const glm::vec3 BakeMax = Comp.Center + Comp.Extents;
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

                // Copy tiles so the worker owns its own data; Comp.Tiles
                // remains the serialized source of truth and isn't touched
                // by the async init.
                TVector<FNavTileData> TilesCopy = Comp.Tiles;
                const glm::vec3 InitOrigin = Comp.Origin;
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

            // Debug draw runs FIRST so it always emits while the mesh is
            // ready, regardless of whether step 5 below decides to early
            // return on empty dirty tiles or saturated rebake jobs.
            if (CVarNavDrawDebug.GetValue())
            {
                const glm::vec4 EdgeColor(0.05f, 1.0f, 0.15f, 1.0f);
                constexpr float EdgeThickness = 2.0f;
                // EnqueueLine (the path Context.DrawDebugLine ends up on)
                // is MPMC-safe, so the visitor can run on every worker.
                Comp.Runtime.Mesh->ParallelForEachTriangle([&Context, EdgeColor](const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, uint8 /*Area*/)
                {
                    const glm::vec3 Lift(0.0f, 0.05f, 0.0f);
                    Context.DrawDebugLine(A + Lift, B + Lift, EdgeColor, EdgeThickness, -1.0f);
                    Context.DrawDebugLine(B + Lift, C + Lift, EdgeColor, EdgeThickness, -1.0f);
                    Context.DrawDebugLine(C + Lift, A + Lift, EdgeColor, EdgeThickness, -1.0f);
                });
            }

            // 3. Hot-swap completed per-tile rebakes.
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

                // Persist a copy into Comp.Tiles BEFORE handing the blob to
                // the runtime mesh. Without this, the serialized tile array
                // would lose its data on every rebake, and any subsequent
                // PIE clone or world save would init from empty blobs (the
                // dtNavMesh's owned copy is unreachable from serialization).
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

            // 4. Detect moved/added/removed source colliders and dirty their tiles.
            THashMap<uint64, FNavSourceEntity> CurrentAABBs;
            CurrentAABBs.reserve(Comp.Runtime.EntityAABBs.size());

            auto MarkDirtyForAABB = [&](const glm::vec3& Mn, const glm::vec3& Mx)
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

            auto VisitSource = [&](uint64 Key, const glm::vec3& Mn, const glm::vec3& Mx)
            {
                CurrentAABBs[Key] = FNavSourceEntity{ Mn, Mx };
                auto It = Comp.Runtime.EntityAABBs.find(Key);
                const bool bNew   = It == Comp.Runtime.EntityAABBs.end();
                const bool bMoved = !bNew && (!Math::IsNearlyEqual(It->second.AABBMin, Mn) || !Math::IsNearlyEqual(It->second.AABBMax, Mx));
                if (bNew || bMoved)
                {
                    if (bMoved)
                    {
                        // Old footprint also gets dirtied so triangles we
                        // were standing on are re-evaluated.
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
                glm::vec3 Mn, Mx;
                if (!ComputeBoxColliderAABB(Box, BoxCheckView.get<STransformComponent>(E), Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Box), Mn, Mx);
            }

            auto SphereCheckView = Context.CreateView<SSphereColliderComponent, STransformComponent>();
            for (entt::entity E : SphereCheckView)
            {
                SSphereColliderComponent& Sphere = SphereCheckView.get<SSphereColliderComponent>(E);
                if (!Sphere.bAffectsNavigation) continue;
                glm::vec3 Mn, Mx;
                if (!ComputeSphereColliderAABB(Sphere, SphereCheckView.get<STransformComponent>(E), Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Sphere), Mn, Mx);
            }

            auto MeshCheckView = Context.CreateView<SMeshColliderComponent, STransformComponent>();
            for (entt::entity E : MeshCheckView)
            {
                SMeshColliderComponent& MC = MeshCheckView.get<SMeshColliderComponent>(E);
                if (!MC.bAffectsNavigation) continue;
                const SStaticMeshComponent* Fallback = Context.GetRegistry().try_get<SStaticMeshComponent>(E);
                glm::vec3 Mn, Mx;
                if (!ComputeMeshColliderAABB(MC, MeshCheckView.get<STransformComponent>(E), Fallback, Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Mesh), Mn, Mx);
            }

            auto CapCheckView = Context.CreateView<SCharacterPhysicsComponent, STransformComponent>();
            for (entt::entity E : CapCheckView)
            {
                SCharacterPhysicsComponent& Cap = CapCheckView.get<SCharacterPhysicsComponent>(E);
                if (!Cap.bAffectsNavigation) continue;
                glm::vec3 Mn, Mx;
                if (!ComputeCapsuleColliderAABB(Cap, CapCheckView.get<STransformComponent>(E), Mn, Mx)) continue;
                VisitSource(PackSourceKey(E, ENavColliderType::Capsule), Mn, Mx);
            }

            // Removed colliders also dirty their last-known tiles.
            for (const auto& [Id, Snap] : Comp.Runtime.EntityAABBs)
            {
                if (CurrentAABBs.find(Id) == CurrentAABBs.end())
                {
                    MarkDirtyForAABB(Snap.AABBMin, Snap.AABBMax);
                }
            }

            Comp.Runtime.EntityAABBs = std::move(CurrentAABBs);

            // 5. Kick rebakes for dirty tiles. Cap concurrent jobs so a
            // burst of movement doesn't saturate the worker pool. The
            // remaining tiles stay dirty and pick up next tick.
            constexpr uint32 MaxConcurrent = 8;
            if (Comp.Runtime.DirtyTiles.empty() || Comp.Runtime.PendingRebakes.size() >= MaxConcurrent)
            {
                return;
            }

            // Snapshot only collider parameter blobs + world matrices on the
            // main thread. Cheap. The per-shape tessellation runs on a worker
            // via the coordinator task below.
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

            struct FBoxSnap     { glm::mat4 World; glm::vec3 HalfExtent; };
            struct FSphereSnap  { glm::mat4 World; float Radius; };
            struct FMeshSnap    { glm::mat4 World; CStaticMesh* Mesh; };
            struct FCapsuleSnap { glm::mat4 World; float HalfHeight; float Radius; };

            struct FInputSnapshot
            {
                TVector<FBoxSnap>     Boxes;
                TVector<FSphereSnap>  Spheres;
                TVector<FMeshSnap>    Meshes;
                TVector<FCapsuleSnap> Capsules;
                glm::vec3             BakeMin;
                glm::vec3             BakeMax;
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
                    Snap->Spheres.push_back({ ColliderToWorld(SphereSnapView.get<STransformComponent>(E), Sphere.TranslationOffset, glm::vec3(0.0f)), Sphere.Radius });
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
                    Snap->Capsules.push_back({ ColliderToWorld(CapSnapView.get<STransformComponent>(E), glm::vec3(0.0f), glm::vec3(0.0f)), Cap.HalfHeight, Cap.Radius });
                }
            }

            // One coordinator task: tessellates collider geometry once on a
            // worker, then ParallelFors the per-tile bakes against the
            // shared input. Inner ParallelFor amortizes wait time across the
            // worker pool.
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
        // Fires when a SNavMeshComponent is added to an entity (inspector,
        // script, code). Initializes Center from the entity's transform so
        // the bake volume defaults to the entity's location rather than the
        // origin.
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

        // Register the construction callback once. Connecting twice no-ops
        // because entt sinks dedupe on the same free-function pointer.
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
            // PendingInit's worker holds its own shared_ptr; clearing the
            // component's slot just stops it from being consumed when the
            // worker eventually finishes.
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

        // Bounds with zero or negative volume produce a single 1x1 grid of
        // empty tiles. Catch it up front so the user gets a clear message
        // instead of a "0 walkable tiles" warning at completion.
        const glm::vec3 Span = Comp.Extents * 2.0f;
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

        // EntityAABB cache populated at bake-completion drain instead of
        // here - the gather's tight per-triangle AABBs would mismatch the
        // change-detector's conservative 8-corner AABBs and cause a
        // spurious tile-rebake storm right after the bake lands.
        Comp.Runtime.EntityAABBs.clear();
        Comp.Runtime.DirtyTiles.clear();
        Comp.Runtime.PendingRebakes.clear();
        Comp.Runtime.ActiveBake = NavMeshBuilder::Bake(std::move(Input));
        Comp.Runtime.State = ENavBakeState::Building;
    }

    namespace Nav
    {
        bool FindPath(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, FNavPath& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->FindPath(Start, End, Filter, Out);
        }

        bool ProjectPoint(const FSystemContext& Context, const glm::vec3& World, const glm::vec3& Extents, const FNavQueryFilter& Filter, glm::vec3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->ProjectPoint(World, Extents, Filter, Out);
        }

        bool Raycast(const FSystemContext& Context, const glm::vec3& Start, const glm::vec3& End, const FNavQueryFilter& Filter, glm::vec3& HitOut)
        {
            FNavMesh* Mesh = FirstReadyNavMesh(Context);
            return Mesh && Mesh->Raycast(Start, End, Filter, HitOut);
        }

        // ---- CWorld flavor -------------------------------------------

        namespace
        {
            // Locate the first SNavMeshComponent whose runtime FNavMesh is
            // ready in this world. Multi-bounds support layers on top later
            // (route by entity, area mask, etc.).
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

        bool FindPath(CWorld* World, const glm::vec3& Start, const glm::vec3& End, FNavPath& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->FindPath(Start, End, Filter, Out);
        }

        bool ProjectPoint(CWorld* World, const glm::vec3& Point, const glm::vec3& Extents, glm::vec3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->ProjectPoint(Point, Extents, Filter, Out);
        }

        bool Raycast(CWorld* World, const glm::vec3& Start, const glm::vec3& End, glm::vec3& OutHit)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->Raycast(Start, End, Filter, OutHit);
        }

        bool FindRandomReachablePoint(CWorld* World, const glm::vec3& Origin, float Radius, glm::vec3& Out)
        {
            FNavMesh* Mesh = FirstReadyNavMeshFromWorld(World);
            FNavQueryFilter Filter;
            return Mesh && Mesh->FindRandomPoint(Origin, Radius, Filter, Out);
        }

        bool IsReachable(CWorld* World, const glm::vec3& From, const glm::vec3& To)
        {
            FNavPath Path;
            return FindPath(World, From, To, Path) && Path.bValid && !Path.bPartial;
        }

        float PathLength(CWorld* World, const glm::vec3& From, const glm::vec3& To)
        {
            FNavPath Path;
            if (!FindPath(World, From, To, Path) || !Path.bValid) return -1.0f;
            float Len = 0.0f;
            for (size_t i = 1; i < Path.Corners.size(); ++i)
            {
                Len += glm::length(Path.Corners[i] - Path.Corners[i - 1]);
            }
            return Len;
        }

        // ---- Lua module ----------------------------------------------

        void RegisterLuaModule(Lua::FRef& Globals)
        {
            Lua::FRef NavTable = Globals.NewTable("Nav");

            // Boolean status / reachability queries.
            NavTable.SetFunction<[](CWorld* W) { return Nav::IsReady(W); }>("IsReady");
            NavTable.SetFunction<[](CWorld* W, glm::vec3 From, glm::vec3 To) { return Nav::IsReachable(W, From, To); }>("IsReachable");

            // Float-returning helpers. PathLength returns < 0 when no path.
            NavTable.SetFunction<[](CWorld* W, glm::vec3 From, glm::vec3 To) { return Nav::PathLength(W, From, To); }>("PathLength");

            // Vec3-returning helpers. Returns the input on failure so Lua
            // code can no-op gracefully; pair with IsReady / IsReachable
            // when validity matters.
            NavTable.SetFunction<[](CWorld* W, glm::vec3 P, glm::vec3 E) -> glm::vec3
            {
                glm::vec3 Out = P;
                Nav::ProjectPoint(W, P, E, Out);
                return Out;
            }>("ProjectPoint");

            NavTable.SetFunction<[](CWorld* W, glm::vec3 S, glm::vec3 E) -> glm::vec3
            {
                glm::vec3 Out = E;
                Nav::Raycast(W, S, E, Out);
                return Out;
            }>("Raycast");

            NavTable.SetFunction<[](CWorld* W, glm::vec3 O, float R) -> glm::vec3
            {
                glm::vec3 Out = O;
                Nav::FindRandomReachablePoint(W, O, R, Out);
                return Out;
            }>("FindRandomReachablePoint");

            // Path corners as a Luau array. Empty when no path exists.
            NavTable.SetFunction<[](CWorld* W, glm::vec3 S, glm::vec3 E) -> TVector<glm::vec3>
            {
                FNavPath Path;
                Nav::FindPath(W, S, E, Path);
                return Path.Corners;
            }>("FindPath");
        }
    }
}
