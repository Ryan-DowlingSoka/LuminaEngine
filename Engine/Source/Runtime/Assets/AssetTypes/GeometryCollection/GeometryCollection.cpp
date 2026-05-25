#include "pch.h"
#include "GeometryCollection.h"
#include <cfloat>
#include <cmath>
#include "EASTL/sort.h"
#include "Assets/AssetTypes/Mesh/Mesh.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Core/Object/Package/Package.h"
#include "Memory/SmartPtr.h"
#include "Renderer/MeshData.h"
#include "Renderer/Vertex.h"

namespace Lumina
{
    namespace
    {
        // Deterministic xorshift32 -- same seed reproduces the same fracture (replay/lockstep friendly).
        struct FRng
        {
            uint32 State;
            explicit FRng(uint32 Seed) : State(Seed ? Seed : 0x9E3779B9u) {}
            uint32 Next() { State ^= State << 13; State ^= State >> 17; State ^= State << 5; return State; }
            float  Unit() { return static_cast<float>(Next() & 0xFFFFFFu) / static_cast<float>(0x1000000); } // [0,1)
        };

        using FFace = TVector<glm::vec3>;
        struct FConvexPoly { TVector<FFace> Faces; };

        FConvexPoly MakeBox(const glm::vec3& Mn, const glm::vec3& Mx)
        {
            auto Quad = [](const glm::vec3& A, const glm::vec3& B, const glm::vec3& C, const glm::vec3& D)
            {
                FFace F; F.push_back(A); F.push_back(B); F.push_back(C); F.push_back(D); return F;
            };

            FConvexPoly P;
            P.Faces.push_back(Quad({ Mn.x, Mn.y, Mn.z }, { Mn.x, Mx.y, Mn.z }, { Mn.x, Mx.y, Mx.z }, { Mn.x, Mn.y, Mx.z }));
            P.Faces.push_back(Quad({ Mx.x, Mn.y, Mn.z }, { Mx.x, Mx.y, Mn.z }, { Mx.x, Mx.y, Mx.z }, { Mx.x, Mn.y, Mx.z }));
            P.Faces.push_back(Quad({ Mn.x, Mn.y, Mn.z }, { Mx.x, Mn.y, Mn.z }, { Mx.x, Mn.y, Mx.z }, { Mn.x, Mn.y, Mx.z }));
            P.Faces.push_back(Quad({ Mn.x, Mx.y, Mn.z }, { Mx.x, Mx.y, Mn.z }, { Mx.x, Mx.y, Mx.z }, { Mn.x, Mx.y, Mx.z }));
            P.Faces.push_back(Quad({ Mn.x, Mn.y, Mn.z }, { Mx.x, Mn.y, Mn.z }, { Mx.x, Mx.y, Mn.z }, { Mn.x, Mx.y, Mn.z }));
            P.Faces.push_back(Quad({ Mn.x, Mn.y, Mx.z }, { Mx.x, Mn.y, Mx.z }, { Mx.x, Mx.y, Mx.z }, { Mn.x, Mx.y, Mx.z }));
            return P;
        }

        // Order coplanar points (lying on a plane with normal N) into a convex CCW loop.
        FFace OrderCoplanarLoop(const TVector<glm::vec3>& Pts, const glm::vec3& N, float Eps)
        {
            TVector<glm::vec3> Unique;
            for (const glm::vec3& P : Pts)
            {
                bool bDup = false;
                for (const glm::vec3& Q : Unique)
                {
                    if (glm::distance(P, Q) <= Eps) { bDup = true; break; }
                }
                if (!bDup) Unique.push_back(P);
            }
            if (Unique.size() < 3) return {};

            glm::vec3 Center(0.0f);
            for (const glm::vec3& P : Unique) Center += P;
            Center /= static_cast<float>(Unique.size());

            const glm::vec3 Ref = glm::abs(N.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            const glm::vec3 U = glm::normalize(glm::cross(N, Ref));
            const glm::vec3 V = glm::cross(N, U);

            eastl::sort(Unique.begin(), Unique.end(), [&](const glm::vec3& A, const glm::vec3& B)
            {
                const float AngleA = std::atan2(glm::dot(A - Center, V), glm::dot(A - Center, U));
                const float AngleB = std::atan2(glm::dot(B - Center, V), glm::dot(B - Center, U));
                return AngleA < AngleB;
            });
            return Unique;
        }

        // Clip the convex polyhedron by the half-space { x : dot(N,x) - D <= 0 }, capping the cut.
        void ClipPolyByPlane(FConvexPoly& Poly, const glm::vec3& N, float D, float Eps)
        {
            TVector<FFace>     NewFaces;
            TVector<glm::vec3> CapPoints;

            for (const FFace& Face : Poly.Faces)
            {
                FFace Out;
                const size_t L = Face.size();
                for (size_t k = 0; k < L; ++k)
                {
                    const glm::vec3& A = Face[k];
                    const glm::vec3& B = Face[(k + 1) % L];
                    const float da = glm::dot(N, A) - D;
                    const float db = glm::dot(N, B) - D;

                    if (da <= Eps)
                    {
                        Out.push_back(A);
                    }
                    if ((da < -Eps && db > Eps) || (da > Eps && db < -Eps))
                    {
                        const float t = da / (da - db);
                        const glm::vec3 P = A + t * (B - A);
                        Out.push_back(P);
                        CapPoints.push_back(P);
                    }
                }
                if (Out.size() >= 3)
                {
                    NewFaces.push_back(Move(Out));
                }
            }

            if (CapPoints.size() >= 3)
            {
                FFace Cap = OrderCoplanarLoop(CapPoints, N, Eps);
                if (Cap.size() >= 3)
                {
                    NewFaces.push_back(Move(Cap));
                }
            }

            Poly.Faces = Move(NewFaces);
        }

        glm::vec3 NewellNormal(const FFace& Face)
        {
            glm::vec3 N(0.0f);
            const size_t L = Face.size();
            for (size_t k = 0; k < L; ++k)
            {
                const glm::vec3& A = Face[k];
                const glm::vec3& B = Face[(k + 1) % L];
                N.x += (A.y - B.y) * (A.z + B.z);
                N.y += (A.z - B.z) * (A.x + B.x);
                N.z += (A.x - B.x) * (A.y + B.y);
            }
            return N;
        }

        // Triangulate the convex polyhedron into a flat-shaded piece (hard edges, outward winding).
        bool BuildPieceFromPoly(const FConvexPoly& Poly, FFracturePiece& Out)
        {
            if (Poly.Faces.size() < 4)
            {
                return false;
            }

            glm::vec3 Centroid(0.0f);
            uint32 Count = 0;
            for (const FFace& Face : Poly.Faces)
            {
                for (const glm::vec3& V : Face) { Centroid += V; ++Count; }
            }
            if (Count == 0)
            {
                return false;
            }
            Centroid /= static_cast<float>(Count);

            glm::vec3 Mn(FLT_MAX);
            glm::vec3 Mx(-FLT_MAX);
            Out.Vertices.clear();
            Out.Indices.clear();

            for (const FFace& SrcFace : Poly.Faces)
            {
                if (SrcFace.size() < 3)
                {
                    continue;
                }

                glm::vec3 Newell = NewellNormal(SrcFace);
                if (glm::length(Newell) < 1e-10f)
                {
                    continue;
                }
                Newell = glm::normalize(Newell);

                glm::vec3 FaceCenter(0.0f);
                for (const glm::vec3& V : SrcFace) FaceCenter += V;
                FaceCenter /= static_cast<float>(SrcFace.size());

                // Reverse the loop if its winding faces inward, so emitted triangles face outward.
                FFace Face = SrcFace;
                glm::vec3 Normal = Newell;
                if (glm::dot(Newell, FaceCenter - Centroid) < 0.0f)
                {
                    eastl::reverse(Face.begin(), Face.end());
                    Normal = -Newell;
                }

                glm::vec3 Tangent = glm::abs(Normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                Tangent = glm::normalize(Tangent - Normal * glm::dot(Tangent, Normal));
                const glm::vec3 Bitangent = glm::cross(Normal, Tangent);

                const uint32 PackedNormal  = PackNormal(Normal);
                const uint32 PackedTangent = PackTangent(Tangent, 1.0f);
                const uint32 PackedColor   = PackColor(glm::vec4(1.0f));

                const uint32 Base = static_cast<uint32>(Out.Vertices.size());
                for (const glm::vec3& P : Face)
                {
                    FVertex Vert;
                    Vert.Position = P;
                    Vert.Normal   = PackedNormal;
                    Vert.Tangent  = PackedTangent;
                    Vert.UV       = glm::packHalf2x16(glm::vec2(glm::dot(P, Tangent), glm::dot(P, Bitangent)));
                    Vert.Color    = PackedColor;
                    Out.Vertices.push_back(Vert);

                    Mn = glm::min(Mn, P);
                    Mx = glm::max(Mx, P);
                }

                // Fan triangulation of the convex loop.
                const uint32 L = static_cast<uint32>(Face.size());
                for (uint32 k = 1; k + 1 < L; ++k)
                {
                    Out.Indices.push_back(Base);
                    Out.Indices.push_back(Base + k);
                    Out.Indices.push_back(Base + k + 1);
                }
            }

            if (Out.Vertices.size() < 4 || Out.Indices.size() < 12)
            {
                return false;
            }

            Out.Center = Centroid;
            Out.Bounds = FAABB(Mn, Mx);
            return true;
        }

        // Pull LOD0 positions + triangles out of the mesh's meshlet data (the scratch vertex
        // streams are dropped after GPU upload, so dequantize from the meshlets like the collider does).
        void GatherMeshGeometry(const CMesh* Mesh, TVector<glm::vec3>& OutPositions, TVector<glm::uvec3>& OutTriangles)
        {
            const FMeshResource& Resource = Mesh->GetMeshResource();
            const FMeshletData&  MD       = Resource.MeshletData;
            if (MD.IsEmpty() || Resource.bSkinnedMesh)
            {
                return;
            }

            Mesh->ForEachSurface([&](const FGeometrySurface& Surface, uint32)
            {
                const uint32 Offset = Surface.LODMeshletOffset[0];
                const uint32 Count  = Surface.LODMeshletCount[0];
                for (uint32 i = 0; i < Count; ++i)
                {
                    const FMeshlet& M       = MD.Meshlets[Offset + i];
                    const uint32 BaseVertex = static_cast<uint32>(OutPositions.size());

                    for (uint32 v = 0; v < M.VertexCount; ++v)
                    {
                        const uint32 P  = MD.MeshletVertices[M.VertexOffset + v].Position;
                        const float qx  = static_cast<float>( P        & 0x3FFu);
                        const float qy  = static_cast<float>((P >> 10) & 0x3FFu);
                        const float qz  = static_cast<float>((P >> 20) & 0x3FFu);
                        const glm::vec3 Pos = MD.MeshOrigin[M.LODIndex] + (glm::vec3(M.LoInt) + glm::vec3(qx, qy, qz)) * MD.MeshGridStep[M.LODIndex];
                        OutPositions.push_back(Pos);
                    }

                    for (uint32 t = 0; t < M.TriangleCount; ++t)
                    {
                        const uint32 Packed = MD.MeshletTriangles[M.TriangleOffset + t];
                        OutTriangles.push_back(glm::uvec3(
                            BaseVertex + ( Packed        & 0xFFu),
                            BaseVertex + ((Packed >>  8) & 0xFFu),
                            BaseVertex + ((Packed >> 16) & 0xFFu)));
                    }
                }
            });
        }

        // Convex-hull supporting planes (outward normal N, offset D; interior is dot(N,x) <= D).
        // A triangle's plane is a hull face iff every vertex sits on one side -- true for convex
        // meshes (all faces) and concave ones (only the hull faces qualify).
        void ComputeHullPlanes(const TVector<glm::vec3>& Positions, const TVector<glm::uvec3>& Triangles, float Tol, TVector<glm::vec4>& OutPlanes)
        {
            auto AddUnique = [&](const glm::vec3& N, float D)
            {
                for (const glm::vec4& Existing : OutPlanes)
                {
                    if (glm::dot(glm::vec3(Existing), N) > 0.999f && glm::abs(Existing.w - D) <= Tol)
                    {
                        return;
                    }
                }
                OutPlanes.push_back(glm::vec4(N, D));
            };

            for (const glm::uvec3& Tri : Triangles)
            {
                const glm::vec3& A = Positions[Tri.x];
                const glm::vec3& B = Positions[Tri.y];
                const glm::vec3& C = Positions[Tri.z];

                glm::vec3 N = glm::cross(B - A, C - A);
                const float Len = glm::length(N);
                if (Len < 1e-12f)
                {
                    continue;
                }
                N /= Len;
                const float D = glm::dot(N, A);

                float MaxProj = -FLT_MAX;
                float MinProj =  FLT_MAX;
                for (const glm::vec3& P : Positions)
                {
                    const float d = glm::dot(N, P);
                    MaxProj = glm::max(MaxProj, d);
                    MinProj = glm::min(MinProj, d);
                }

                if (MaxProj <= D + Tol)       AddUnique( N,  D);   // mesh on the negative side -> outward = N
                else if (MinProj >= D - Tol)  AddUnique(-N, -D);   // mesh on the positive side -> outward = -N
            }
        }
    }

    void Fracture::GenerateConvexFracture(const CMesh* SourceMesh, const FFractureSettings& Settings, TVector<FFracturePiece>& OutPieces)
    {
        OutPieces.clear();
        if (SourceMesh == nullptr)
        {
            return;
        }

        const FAABB Bounds = SourceMesh->GetAABB();
        const glm::vec3 Mn = Bounds.Min;
        const glm::vec3 Mx = Bounds.Max;
        const glm::vec3 Size = glm::max(Mx - Mn, glm::vec3(1e-4f));
        const float Diag    = glm::length(Size);
        const float Eps     = Diag * 1e-5f + 1e-6f;
        const float HullTol = Diag * 1e-3f;

        // Hull supporting planes from the mesh triangles.
        TVector<glm::vec4> HullPlanes;
        size_t NumPos = 0;
        size_t NumTri = 0;
        {
            TVector<glm::vec3>  Positions;
            TVector<glm::uvec3> Triangles;
            GatherMeshGeometry(SourceMesh, Positions, Triangles);
            NumPos = Positions.size();
            NumTri = Triangles.size();
            if (Positions.size() >= 4 && !Triangles.empty())
            {
                ComputeHullPlanes(Positions, Triangles, HullTol, HullPlanes);
            }
        }

        // Clip the bounds box down to the hull once; every cell starts from this shape.
        FConvexPoly Hull = MakeBox(Mn, Mx);
        for (const glm::vec4& Plane : HullPlanes)
        {
            ClipPolyByPlane(Hull, glm::vec3(Plane), Plane.w, Eps);
        }
        const size_t FacesAfterClip = Hull.Faces.size();
        if (Hull.Faces.size() < 4)
        {
            Hull = MakeBox(Mn, Mx);   // hull clip collapsed (bad data) -> fall back to box
        }

        LOG_INFO("[Fracture] meshletVerts={} tris={} hullPlanes={} hullFacesAfterClip={} boundsDiag={}",
            NumPos, NumTri, HullPlanes.size(), FacesAfterClip, Diag);

        auto InsideHull = [&](const glm::vec3& S) -> bool
        {
            for (const glm::vec4& Plane : HullPlanes)
            {
                if (glm::dot(glm::vec3(Plane), S) > Plane.w + HullTol)
                {
                    return false;
                }
            }
            return true;
        };

        const int32 N = glm::clamp(Settings.NumPieces, 2, 512);
        FRng Rng(Settings.Seed);

        // Rejection-sample seeds inside the hull so cells fill the shape evenly.
        TVector<glm::vec3> Seeds;
        Seeds.reserve(N);
        for (int32 i = 0; i < N; ++i)
        {
            glm::vec3 S = Mn + Size * glm::vec3(Rng.Unit(), Rng.Unit(), Rng.Unit());
            for (int32 Attempt = 0; Attempt < 64 && !InsideHull(S); ++Attempt)
            {
                S = Mn + Size * glm::vec3(Rng.Unit(), Rng.Unit(), Rng.Unit());
            }
            Seeds.push_back(S);
        }

        OutPieces.reserve(N);
        for (int32 i = 0; i < N; ++i)
        {
            FConvexPoly Poly = Hull;   // start from the hull, not the box
            for (int32 j = 0; j < N && !Poly.Faces.empty(); ++j)
            {
                if (j == i)
                {
                    continue;
                }
                const glm::vec3 Delta = Seeds[j] - Seeds[i];
                const float Length = glm::length(Delta);
                if (Length < Eps)
                {
                    continue;
                }
                const glm::vec3 Normal = Delta / Length;
                const glm::vec3 Mid = 0.5f * (Seeds[i] + Seeds[j]);
                ClipPolyByPlane(Poly, Normal, glm::dot(Normal, Mid), Eps);
            }

            FFracturePiece Piece;
            if (BuildPieceFromPoly(Poly, Piece))
            {
                OutPieces.push_back(Move(Piece));
            }
        }
    }

    CStaticMesh* Fracture::BuildPieceMesh(const FFracturePiece& Piece, const TVector<TObjectPtr<CMaterialInterface>>& Materials, const char* DebugName)
    {
        if (Piece.Vertices.empty() || Piece.Indices.empty())
        {
            return nullptr;
        }

        TUniquePtr<FMeshResource> Resource = MakeUnique<FMeshResource>();
        Resource->ReserveVertices(Piece.Vertices.size());
        for (const FVertex& V : Piece.Vertices)
        {
            Resource->AppendVertex(V);
        }
        Resource->Indices = Piece.Indices;

        FGeometrySurface Surface;
        Surface.ID            = "Piece";
        Surface.IndexCount    = static_cast<uint32>(Piece.Indices.size());
        Surface.StartIndex    = 0;
        Surface.MaterialIndex = 0;
        Resource->GeometrySurfaces.push_back(Surface);

        CStaticMesh* Mesh = NewObject<CStaticMesh>(CPackage::GetTransientPackage(), DebugName);
        Mesh->Materials = Materials;
        if (Mesh->Materials.empty())
        {
            Mesh->Materials.resize(1);
        }
        Mesh->SetMeshResource(Move(Resource));
        return Mesh;
    }

    void CGeometryCollection::Serialize(FArchive& Ar)
    {
        Super::Serialize(Ar);
        Ar << Data;
    }

    int32 CGeometryCollection::Rebuild()
    {
        CStaticMesh* Source = SourceMesh.Get();
        if (Source == nullptr)
        {
            Data.Pieces.clear();
            return 0;
        }

        FFractureSettings Settings;
        Settings.NumPieces = NumPieces;
        Settings.Seed      = static_cast<uint32>(Seed);

        Data.SourceBounds = Source->GetAABB();
        Fracture::GenerateConvexFracture(Source, Settings, Data.Pieces);
        Materials = Source->Materials;
        return GetNumPieces();
    }

    CGeometryCollection* CGeometryCollection::GenerateFromMesh(CStaticMesh* Source, const FFractureSettings& Settings, CObject* Outer)
    {
        if (Source == nullptr)
        {
            return nullptr;
        }

        CPackage* Package = Outer ? Outer->GetPackage() : CPackage::GetTransientPackage();
        CGeometryCollection* Collection = NewObject<CGeometryCollection>(Package, "GeometryCollection");

        Collection->SourceMesh = Source;
        Collection->NumPieces  = Settings.NumPieces;
        Collection->Seed       = static_cast<int32>(Settings.Seed);
        Collection->Rebuild();
        return Collection;
    }
}
