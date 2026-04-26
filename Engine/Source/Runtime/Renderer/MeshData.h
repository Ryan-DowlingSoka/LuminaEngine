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

        // Range into FMeshResource::MeshletData.Meshlets.
        uint32  MeshletOffset = 0;
        uint32  MeshletCount  = 0;

        friend FArchive& operator << (FArchive& Ar, FGeometrySurface& Data)
        {
            Ar << Data.ID;
            Ar << Data.IndexCount;
            Ar << Data.StartIndex;
            Ar << Data.MaterialIndex;
            Ar << Data.MeshletOffset;
            Ar << Data.MeshletCount;

            return Ar;
        }
    };

    struct RUNTIME_API FMeshResource : INonCopyable
    {
        using FVertexVariant = TVariant<TVector<FVertex>, TVector<FSkinnedVertex>>;

        struct FMeshBuffers
        {
            FRHIBufferRef MeshletBuffer;
            FRHIBufferRef MeshletBoundsBuffer;
            FRHIBufferRef MeshletVertexBuffer;
            FRHIBufferRef MeshletTriangleBuffer;
            FRHIBufferRef MeshletHeaderBuffer;
        };

        FName                       Name;
        // Import-time scratch; not serialized. Dropped after GenerateMeshlets.
        FVertexVariant              Vertices;
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
            Ar << Data.bSkinnedMesh;
            Ar << Data.GeometrySurfaces;
            Ar << Data.MeshletData;
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

        // Transient: set in the import dialog so the user can deselect a
        // specific skeleton (e.g. an FBX with multiple rigs where only one
        // is wanted). Intentionally not serialized.
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
