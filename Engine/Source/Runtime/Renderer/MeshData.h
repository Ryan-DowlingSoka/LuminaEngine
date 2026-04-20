#pragma once

#include "RenderResource.h"
#include "Containers/Array.h"
#include "Core/Serialization/Archiver.h"
#include "Core/Utils/NonCopyable.h"
#include "Renderer/Vertex.h"

namespace Lumina
{
    // Meshlet sizing — 64 verts / 124 tris is the AMD/NV mesh-shader sweet spot
    // and satisfies meshoptimizer limits (max_vertices <= 256, max_triangles <=
    // 512, max_triangles divisible by 4).
    constexpr uint32 MESHLET_MAX_VERTICES       = 64;
    constexpr uint32 MESHLET_MAX_TRIANGLES      = 124;
    // Vertex count per meshlet draw invocation — the base VS walks TriangleCount*3
    // verts and emits degenerates for the remaining slots so every meshlet
    // shares the same VertexCount in its indirect args.
    constexpr uint32 MESHLET_VERTICES_PER_DRAW  = MESHLET_MAX_TRIANGLES * 3;

    // Single meshlet descriptor — offsets into the flat arrays on FMeshletData.
    // VertexOffset indexes into FMeshletData::MeshletVertices which in turn
    // indexes FMeshResource::Vertices. TriangleOffset indexes into
    // FMeshletData::MeshletTriangles which is packed 3 bytes per triangle with
    // the meshlet-local (0..VertexCount-1) vertex index.
    struct FMeshlet
    {
        uint32 VertexOffset;
        uint32 TriangleOffset;
        uint32 VertexCount;
        uint32 TriangleCount;
    };

    // Per-meshlet culling volumes. Bounding sphere for frustum/occlusion,
    // cone for backface culling. 16-byte alignment keeps a future GPU upload
    // aligned with a tight std430 layout.
    struct alignas(16) FMeshletBounds
    {
        glm::vec3 Center;
        float     Radius;
        glm::vec3 ConeApex;
        float     ConeCutoff;   // = cos(angle / 2)
        glm::vec3 ConeAxis;
        float     _Pad0;
    };

    struct FMeshletData
    {
        TVector<FMeshlet>        Meshlets;
        TVector<uint32>          MeshletVertices;    // indices into FMeshResource::Vertices
        TVector<uint8>           MeshletTriangles;   // micro-indices into the meshlet's vertex slice
        TVector<FMeshletBounds>  MeshletBounds;

        FORCEINLINE bool IsEmpty() const { return Meshlets.empty(); }

        FORCEINLINE void Clear()
        {
            Meshlets.clear();
            MeshletVertices.clear();
            MeshletTriangles.clear();
            MeshletBounds.clear();
        }
    };

    // GPU-side descriptor for one mesh's meshlet data. Uploaded once per mesh
    // into a tiny per-mesh SSBO; FGPUInstance carries the buffer-device address
    // so the meshlet cull pass and base VS can reach all four flat arrays with
    // a single pointer indirection.
    struct alignas(16) FMeshletHeaderGPU
    {
        uint64 MeshletsAddress;           // FMeshlet*
        uint64 BoundsAddress;             // FMeshletBounds*
        uint64 VerticesAddress;           // uint32*
        uint64 TrianglesAddress;          // uint32* (packed 4x uint8)
    };

    struct FGeometrySurface final
    {
        FName   ID;
        uint32  IndexCount = 0;
        uint32  StartIndex = 0;
        int16   MaterialIndex = -1;

        // Per-surface meshlet range into FMeshResource::MeshletData.Meshlets.
        // Runtime-only — regenerated in PostLoad alongside the meshlet arrays,
        // same pattern as ShadowIndices. Not written into the archive stream
        // so existing assets on disk keep deserializing unchanged.
        uint32  MeshletOffset = 0;
        uint32  MeshletCount  = 0;

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
        using FVertexVariant = TVariant<TVector<FVertex>, TVector<FSkinnedVertex>>;

        struct FMeshBuffers
        {
            FRHIBufferRef VertexBuffer;
            FRHIBufferRef IndexBuffer;
            FRHIBufferRef ShadowIndexBuffer;

            // Meshlet data uploaded once per mesh. MeshletHeader stores BDAs to
            // the four arrays so FGPUInstance can reach them with one pointer.
            FRHIBufferRef MeshletBuffer;          // TVector<FMeshlet>
            FRHIBufferRef MeshletBoundsBuffer;    // TVector<FMeshletBounds>
            FRHIBufferRef MeshletVertexBuffer;    // TVector<uint32>
            FRHIBufferRef MeshletTriangleBuffer;  // Packed TVector<uint8> (4 per uint32)
            FRHIBufferRef MeshletHeaderBuffer;    // FMeshletHeaderGPU[1]
        };
        
        FName                       Name;
        FVertexVariant              Vertices;
        TVector<uint32>             Indices;
        TVector<uint32>             ShadowIndices;
        TVector<FGeometrySurface>   GeometrySurfaces;
        FMeshletData                MeshletData;
        FMeshBuffers                MeshBuffers;
        bool                        bSkinnedMesh = false;
        FRHIInputLayoutRef          VertexLayout;
        
        FORCEINLINE size_t GetNumSurfaces() const { return GeometrySurfaces.size(); }
        
        FORCEINLINE bool IsSurfaceIndexValid(size_t Slot) const
        {
            return Slot < GetNumSurfaces();
        }
        
        FORCEINLINE const FGeometrySurface& GetSurface(size_t Slot) const
        {
            return GeometrySurfaces[Slot];
        }
        
        template<typename T>
        NODISCARD TVector<T>& GetVertexDataAs() { return eastl::get<TVector<T>>(Vertices); }
        
        FORCEINLINE size_t GetNumVertices() const
        {
            return eastl::visit([&](auto& Vector) { return Vector.size(); }, Vertices);
        }
        
        FORCEINLINE size_t GetNumIndices() const
        {
            return Indices.size();
        }
        
        FORCEINLINE size_t GetNumTriangles() const
        {
            return Indices.size() / 3;
        }
        
        void SetPositionAt(size_t Index, glm::vec3 Position)
        {
            eastl::visit([&]<typename T0>(T0& Vector)
            {
                Vector[Index].Position = Position;
            }, Vertices);
        }
        
        void SetNormalAt(size_t Index, uint32 Normal)
        {
            eastl::visit([&]<typename T0>(T0& Vector)
            {
                Vector[Index].Normal = Normal;
            }, Vertices);
        }
        
        void SetUVAt(size_t Index, glm::vec2 UV)
        {
            eastl::visit([&]<typename T0>(T0& Vector)
            {
                Vector[Index].UV = glm::packHalf2x16(UV);
            }, Vertices);
        }
        
        void SetColorAt(size_t Index, uint32 Color)
        {
            eastl::visit([&]<typename T0>(T0& Vector)
            {
                Vector[Index].Color = Color;
            }, Vertices);
        }
        
        void SetJointWeightsAt(size_t Index, glm::u8vec4 Weights)
        {
            eastl::get<eastl::vector<FSkinnedVertex>>(Vertices)[Index].JointWeights = Weights;
        }
        
        void SetJointIndicesAt(size_t Index, glm::u8vec4 InIndices)
        {
            eastl::get<eastl::vector<FSkinnedVertex>>(Vertices)[Index].JointIndices = InIndices;
        }
        
        glm::vec3 GetPositionAt(size_t Index) const
        {
            return eastl::visit([&]<typename T0>(const T0& Vector)
            {
                return Vector[Index].Position;
            }, Vertices);
        }
        
        uint32 GetNormalAt(size_t Index) const
        {
            return eastl::visit([&]<typename T0>(const T0& Vector)
            {
                return Vector[Index].Normal;
            }, Vertices);
        }
        
        glm::vec2 GetUVAt(size_t Index) const
        {
            return eastl::visit([&]<typename T0>(const T0& Vector)
            {
                return glm::unpackHalf2x16(Vector[Index].UV);
            }, Vertices);
        }
        
        uint32 GetColorAt(size_t Index) const
        {
            return eastl::visit([&]<typename T0>(const T0& Vector)
            {
                return Vector[Index].Color;
            }, Vertices);
        }
        
        glm::u8vec4 GetJointIndicesAt(size_t Index) const
        {
            return eastl::get<eastl::vector<FSkinnedVertex>>(Vertices)[Index].JointIndices;
        }
        
        glm::u8vec4 GetJointWeightsAt(size_t Index) const
        {
            return eastl::get<eastl::vector<FSkinnedVertex>>(Vertices)[Index].JointWeights;
        }
        
        template<typename TVertex>
        const glm::vec3& GetPosition(const TVertex& V) const
        {
            return V.Position;
        }
        
        template<typename TVertex>
        void ExpandBounds(const TVertex& Vertex, FAABB& BoundingBox)
        {
            const glm::vec3& P = GetPosition(Vertex);
        
            BoundingBox.Min = glm::min(BoundingBox.Min, P);
            BoundingBox.Max = glm::max(BoundingBox.Max, P);
        }
        
        FORCEINLINE size_t GetVertexTypeSize() const
        {
            return eastl::visit([&]<typename T0>(const T0& Vector)
            {
                using VertexT = eastl::decay_t<T0>::value_type;
                return sizeof(VertexT);
            }, Vertices);
        }
        
        FORCEINLINE NODISCARD bool IsSkinnedMesh() const { return bSkinnedMesh; }

        FORCEINLINE NODISCARD void* GetVertexData()
        {
            return eastl::visit([&]<typename T0>(T0& Vector)
            {
                return reinterpret_cast<void*>(Vector.data());
            }, Vertices);
        }
        
        friend FArchive& operator << (FArchive& Ar, FMeshResource& Data)
        {
            Ar << Data.Name;
            Ar << Data.bSkinnedMesh; // Intentionally done out of order.
            
            if (Ar.IsWriting())
            {
                if (Data.bSkinnedMesh)
                {
                    Ar << Data.GetVertexDataAs<FSkinnedVertex>();
                }
                else
                {
                    Ar << Data.GetVertexDataAs<FVertex>();
                }
            }
            else
            {
                if (Data.bSkinnedMesh)
                {
                    TVector<FSkinnedVertex> SkinnedVertices;
                    Ar << SkinnedVertices;
                    Data.Vertices = Move(SkinnedVertices);
                }
                else
                {
                    TVector<FVertex> StaticVertices;
                    Ar << StaticVertices;
                    Data.Vertices = Move(StaticVertices);
                }
            }
            
            Ar << Data.Indices;
            // ShadowIndices intentionally NOT serialized: it is derivable from
            // Indices + vertex positions, and inserting it into the archive
            // stream would break backwards compatibility with every mesh on
            // disk. Regenerated in CMesh::PostLoad via GenerateShadowBuffers.
            Ar << Data.GeometrySurfaces;

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
    
        // Get the parent bone, or nullptr if root
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
    
        // Get all children of a bone
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
    
        // Get root bones (bones with no parent)
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
