#pragma once

#include "RenderResource.h"
#include "Containers/Array.h"
#include "Core/Serialization/Archiver.h"
#include "Core/Utils/NonCopyable.h"
#include "Renderer/Vertex.h"

namespace Lumina
{
    // 64 verts / 124 tris = AMD/NV mesh-shader sweet spot, satisfies meshopt
    // limits, and TriangleCount*3 fits the VS-emulation indirect arg count.
    constexpr uint32 MESHLET_MAX_VERTICES       = 64;
    constexpr uint32 MESHLET_MAX_TRIANGLES      = 124;
    constexpr uint32 MESHLET_VERTICES_PER_DRAW  = MESHLET_MAX_TRIANGLES * 3;

    // LOD 0 is full detail. Default ladder uses meshopt_simplify for 0-3 and
    // meshopt_simplifySloppy for 4-5.
    constexpr uint32 MAX_MESH_LODS              = 6;

    // Sloppy LODs (4-5) can produce holes that become shadow light-leaks; cap
    // shadow casters to topology-preserving LODs.
    constexpr uint32 MAX_SHADOW_LOD             = 3;

    // LoInt: meshlet's quantization origin in mesh-global grid units.
    // TriangleOffset is in dwords (3 micro-indices per dword).
    struct alignas(16) FMeshlet
    {
        uint32     VertexOffset;
        uint32     TriangleOffset;
        uint32     VertexCount;
        uint32     TriangleCount;
        glm::ivec3 LoInt;
        uint32     _Pad0;

        friend FArchive& operator<<(FArchive& Ar, FMeshlet& Data)
        {
            Ar << Data.VertexOffset;
            Ar << Data.TriangleOffset;
            Ar << Data.VertexCount;
            Ar << Data.TriangleCount;
            Ar << Data.LoInt;
            Ar << Data._Pad0;
            return Ar;
        }
    };

    // Sphere for frustum/occlusion, cone for backface culling.
    struct alignas(16) FMeshletBounds
    {
        glm::vec3 Center;
        float     Radius;
        glm::vec3 ConeApex;
        float     ConeCutoff;   // = cos(angle / 2)
        glm::vec3 ConeAxis;
        float     _Pad0;

        friend FArchive& operator<<(FArchive& Ar, FMeshletBounds& Data)
        {
            Ar << Data.Center;
            Ar << Data.Radius;
            Ar << Data.ConeApex;
            Ar << Data.ConeCutoff;
            Ar << Data.ConeAxis;
            Ar << Data._Pad0;
            return Ar;
        }
    };

    struct FMeshletData
    {
        TVector<FMeshlet>               Meshlets;
        // Exactly one of these is populated, selected by bSkinnedMesh.
        TVector<FMeshletVertex>         MeshletVertices;
        TVector<FMeshletSkinnedVertex>  MeshletSkinnedVertices;
        TVector<uint32>                 MeshletTriangles;
        TVector<FMeshletBounds>         MeshletBounds;

        // Mesh-global integer-grid basis. GridStep sized so any meshlet's
        // extent fits in <=1023 cells per axis.
        glm::vec3                       MeshOrigin   = glm::vec3(0.0f);
        glm::vec3                       MeshGridStep = glm::vec3(0.0f);

        FORCEINLINE bool IsEmpty() const { return Meshlets.empty(); }

        FORCEINLINE void Clear()
        {
            Meshlets.clear();
            MeshletVertices.clear();
            MeshletSkinnedVertices.clear();
            MeshletTriangles.clear();
            MeshletBounds.clear();
            MeshOrigin   = glm::vec3(0.0f);
            MeshGridStep = glm::vec3(0.0f);
        }

        friend FArchive& operator<<(FArchive& Ar, FMeshletData& Data)
        {
            Ar << Data.Meshlets;
            Ar << Data.MeshletVertices;
            Ar << Data.MeshletSkinnedVertices;
            Ar << Data.MeshletTriangles;
            Ar << Data.MeshletBounds;
            Ar << Data.MeshOrigin;
            Ar << Data.MeshGridStep;
            return Ar;
        }
    };

    // Per-mesh GPU header. Reached through FGPUInstance's MeshletHeader BDA.
    struct alignas(16) FMeshletHeaderGPU
    {
        uint64    MeshletsAddress;           // FMeshlet*
        uint64    BoundsAddress;             // FMeshletBounds*
        uint64    VerticesAddress;           // uint32*
        uint64    TrianglesAddress;          // uint32*
        glm::vec4 MeshOriginAndPad;          // xyz = grid origin
        glm::vec4 MeshGridStepAndPad;        // xyz = grid cell size
    };

    struct FGeometrySurface final
    {
        FName   ID;
        uint32  IndexCount = 0;
        uint32  StartIndex = 0;
        int16   MaterialIndex = -1;

        // Per-LOD meshlet ranges into MeshletData.Meshlets; NumLODs >= 1.
        uint32  NumLODs                              = 1;
        uint32  LODMeshletOffset[MAX_MESH_LODS]      = {};
        uint32  LODMeshletCount[MAX_MESH_LODS]       = {};
        // distance/radius threshold at which LOD i becomes active (monotonic, [0] unused).
        float   LODScreenThreshold[MAX_MESH_LODS]    = {};

        friend FArchive& operator << (FArchive& Ar, FGeometrySurface& Data)
        {
            Ar << Data.ID;
            Ar << Data.IndexCount;
            Ar << Data.StartIndex;
            Ar << Data.MaterialIndex;

            return Ar;
        }
    };

    struct RUNTIME_API FMeshResource : INonCopyable
    {
        struct FMeshBuffers
        {
            FRHIBufferRef MeshletBuffer;
            FRHIBufferRef MeshletBoundsBuffer;
            FRHIBufferRef MeshletVertexBuffer;
            FRHIBufferRef MeshletTriangleBuffer;
            FRHIBufferRef MeshletHeaderBuffer;
        };

        FName                       Name;

        // Import-time scratch vertex streams (structure-of-arrays); not serialized,
        // dropped after GenerateMeshlets. All active streams stay parallel and equal
        // length. Joint streams are populated only when bSkinnedMesh, else empty.
        TVector<glm::vec3>          Positions;
        TVector<uint32>             Normals;        // octahedral pack (PackNormal)
        TVector<uint32>             Tangents;       // octahedral + handedness (PackTangent)
        TVector<uint32>             UVs;            // packHalf2x16
        TVector<uint32>             Colors;         // RGBA8 (PackColor)
        TVector<glm::u8vec4>        JointIndices;
        TVector<glm::u8vec4>        JointWeights;

        TVector<uint32>             Indices;
        TVector<FGeometrySurface>   GeometrySurfaces;
        FMeshletData                MeshletData;
        FMeshBuffers                MeshBuffers;
        bool                        bSkinnedMesh = false;

        // Source scene-graph world transform; baked into vertices at merge time.
        glm::mat4                   ImportTransform = glm::mat4(1.0f);

        FORCEINLINE size_t GetNumSurfaces() const { return GeometrySurfaces.size(); }

        FORCEINLINE bool IsSurfaceIndexValid(size_t Slot) const
        {
            return Slot < GetNumSurfaces();
        }

        FORCEINLINE const FGeometrySurface& GetSurface(size_t Slot) const
        {
            return GeometrySurfaces[Slot];
        }

        FORCEINLINE size_t GetNumVertices() const { return Positions.size(); }
        FORCEINLINE size_t GetNumIndices()  const { return Indices.size(); }
        FORCEINLINE size_t GetNumTriangles() const { return Indices.size() / 3; }
        FORCEINLINE NODISCARD bool IsSkinnedMesh() const { return bSkinnedMesh; }

        // Synthetic interleaved vertex size; only meshopt fetch/overdraw analysis needs it.
        FORCEINLINE size_t GetVertexTypeSize() const
        {
            return bSkinnedMesh ? sizeof(FSkinnedVertex) : sizeof(FVertex);
        }

        // --- stream sizing -------------------------------------------------
        void ResizeVertices(size_t N)
        {
            Positions.resize(N);
            Normals.resize(N);
            Tangents.resize(N);
            UVs.resize(N);
            Colors.resize(N);
            if (bSkinnedMesh)
            {
                JointIndices.resize(N);
                JointWeights.resize(N);
            }
        }

        void ReserveVertices(size_t N)
        {
            Positions.reserve(N);
            Normals.reserve(N);
            Tangents.reserve(N);
            UVs.reserve(N);
            Colors.reserve(N);
            if (bSkinnedMesh)
            {
                JointIndices.reserve(N);
                JointWeights.reserve(N);
            }
        }

        void ClearVertices()
        {
            auto Drop = [](auto& V) { V.clear(); V.shrink_to_fit(); };
            Drop(Positions);
            Drop(Normals);
            Drop(Tangents);
            Drop(UVs);
            Drop(Colors);
            Drop(JointIndices);
            Drop(JointWeights);
        }

        void AppendVertex(const FVertex& V)
        {
            Positions.push_back(V.Position);
            Normals.push_back(V.Normal);
            Tangents.push_back(V.Tangent);
            UVs.push_back(V.UV);
            Colors.push_back(V.Color);
        }

        void AppendVertex(const FSkinnedVertex& V)
        {
            AppendVertex(static_cast<const FVertex&>(V));
            JointIndices.push_back(V.JointIndices);
            JointWeights.push_back(V.JointWeights);
        }

        // --- per-vertex accessors (thin; direct stream indexing) -----------
        FORCEINLINE glm::vec3 GetPositionAt(size_t Index) const { return Positions[Index]; }
        FORCEINLINE void SetPositionAt(size_t Index, glm::vec3 Position) { Positions[Index] = Position; }

        FORCEINLINE uint32 GetNormalAt(size_t Index) const { return Normals[Index]; }
        FORCEINLINE void SetNormalAt(size_t Index, uint32 Normal) { Normals[Index] = Normal; }

        FORCEINLINE uint32 GetTangentAt(size_t Index) const { return Tangents[Index]; }
        FORCEINLINE void SetTangentAt(size_t Index, uint32 Tangent) { Tangents[Index] = Tangent; }

        FORCEINLINE glm::vec2 GetUVAt(size_t Index) const { return glm::unpackHalf2x16(UVs[Index]); }
        FORCEINLINE void SetUVAt(size_t Index, glm::vec2 UV) { UVs[Index] = glm::packHalf2x16(UV); }

        FORCEINLINE uint32 GetColorAt(size_t Index) const { return Colors[Index]; }
        FORCEINLINE void SetColorAt(size_t Index, uint32 Color) { Colors[Index] = Color; }

        FORCEINLINE glm::u8vec4 GetJointIndicesAt(size_t Index) const { return JointIndices[Index]; }
        FORCEINLINE void SetJointIndicesAt(size_t Index, glm::u8vec4 InIndices) { JointIndices[Index] = InIndices; }

        FORCEINLINE glm::u8vec4 GetJointWeightsAt(size_t Index) const { return JointWeights[Index]; }
        FORCEINLINE void SetJointWeightsAt(size_t Index, glm::u8vec4 Weights) { JointWeights[Index] = Weights; }

        friend FArchive& operator << (FArchive& Ar, FMeshResource& Data)
        {
            Ar << Data.Name;
            Ar << Data.bSkinnedMesh;
            Ar << Data.GeometrySurfaces;
            Ar << Data.MeshletData;

            // Per-surface LOD payload; pre-LOD assets must be re-imported.
            for (FGeometrySurface& Surface : Data.GeometrySurfaces)
            {
                Ar << Surface.NumLODs;
                for (uint32 i = 0; i < MAX_MESH_LODS; ++i)
                {
                    Ar << Surface.LODMeshletOffset[i];
                    Ar << Surface.LODMeshletCount[i];
                    Ar << Surface.LODScreenThreshold[i];
                }
            }

            return Ar;
        }
    };
    
    
    struct RUNTIME_API FSkeletonResource : INonCopyable
    {
        struct FBoneInfo
        {
            FName Name;
            int32 ParentIndex;           // -1 for root bone
            glm::mat4 InvBindMatrix;     // Inverse bind pose matrix
            glm::mat4 LocalTransform;    // Local transform (relative to parent)
        
            friend FArchive& operator << (FArchive& Ar, FBoneInfo& Data)
            {
                Ar << Data.Name;
                Ar << Data.ParentIndex;
                Ar << Data.InvBindMatrix;
                Ar << Data.LocalTransform;
                return Ar;
            }
        };
        
        FName Name;
        TVector<FBoneInfo> Bones;
        THashMap<FName, int32> BoneNameToIndex;

        // Transient import-dialog flag; not serialized.
        bool bShouldImport = true;
        
        FORCEINLINE int32 GetNumBones() const 
        { 
            return (int32)Bones.size(); 
        }
    
        FORCEINLINE int32 FindBoneIndex(const FName& BoneName) const
        {
            auto It = BoneNameToIndex.find(BoneName);
            return It != BoneNameToIndex.end() ? It->second : INDEX_NONE;
        }
    
        FORCEINLINE bool IsBoneIndexValid(int32 BoneIndex) const
        {
            return BoneIndex >= 0 && BoneIndex < GetNumBones();
        }
    
        FORCEINLINE const FBoneInfo& GetBone(int32 BoneIndex) const
        {
            return Bones[BoneIndex];
        }
        
        FORCEINLINE FBoneInfo& GetBone(int32 BoneIndex)
        {
            return Bones[BoneIndex];
        }
    
        FORCEINLINE const FBoneInfo* GetParentBone(int32 BoneIndex) const
        {
            if (!IsBoneIndexValid(BoneIndex))
            {
                return nullptr;
            }

            int32 ParentIdx = Bones[BoneIndex].ParentIndex;
            if (ParentIdx < 0)
            {
                return nullptr;
            }

            return &Bones[ParentIdx];
        }
    
        TVector<int32> GetChildBones(int32 BoneIndex) const
        {
            TVector<int32> Children;
            for (int32 i = 0; i < GetNumBones(); ++i)
            {
                if (Bones[i].ParentIndex == BoneIndex)
                {
                    Children.push_back(i);
                }
            }
            return Children;
        }
    
        TVector<int32> GetRootBones() const
        {
            TVector<int32> Roots;
            for (int32 i = 0; i < GetNumBones(); ++i)
            {
                if (Bones[i].ParentIndex < 0)
                {
                    Roots.push_back(i);
                }
            }
            return Roots;
        }
        
        friend FArchive& operator << (FArchive& Ar, FSkeletonResource& Data)
        {
            Ar << Data.Name;
            Ar << Data.Bones;
            Ar << Data.BoneNameToIndex;
            
            return Ar;
        }
    };
}
