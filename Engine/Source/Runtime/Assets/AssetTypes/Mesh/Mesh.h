#pragma once

#include "Core/Math/AABB.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Renderer/MeshData.h"
#include "Memory/SmartPtr.h"
#include "Mesh.generated.h"

namespace Lumina
{
    class CMaterialInterface;
    class CMaterialInstance;
    class CMaterial;
}

namespace Lumina
{
    REFLECT()
    class RUNTIME_API CMesh : public CObject
    {
        GENERATED_BODY()
        
        friend class CMeshFactory;
        
    public:
        
        void Serialize(FArchive& Ar) override;
        void PostLoad() override;

        bool IsReadyForRender() const;

        void GenerateBoundingBox();
        void GenerateGPUBuffers();

        uint32 GetNumMaterials() const { return (uint32)Materials.size(); }
        CMaterialInterface* GetMaterialAtSlot(size_t Slot) const;
        void SetMaterialAtSlot(size_t Slot, CMaterialInterface* NewMaterial);
        
        FORCEINLINE const FGeometrySurface& GetSurface(size_t Slot) const { return MeshResources->GeometrySurfaces[Slot]; }
        FORCEINLINE FMeshResource& GetMeshResource() const { return *MeshResources.get(); }

        void SetMeshResource(TUniquePtr<FMeshResource>&& NewResource);
        
        FORCEINLINE const FMeshResource::FMeshBuffers& GetMeshBuffers() const { return MeshResources->MeshBuffers; }
        FORCEINLINE const FRHIBufferRef& GetVertexBuffer() const { return MeshResources->MeshBuffers.VertexBuffer; }
        FORCEINLINE const FRHIBufferRef& GetIndexBuffer() const { return MeshResources->MeshBuffers.IndexBuffer; }
        FORCEINLINE const FRHIBufferRef& GetShadowIndexBuffer() const { return MeshResources->MeshBuffers.ShadowIndexBuffer; }
        
        virtual bool IsSkinned() const { return false; }
        
        FUNCTION(Script)
        FORCEINLINE SIZE_T GetNumVertices() const { return MeshResources->GetNumVertices(); }
        
        FUNCTION(Script)
        FORCEINLINE SIZE_T GetNumIndices() const { return MeshResources->GetNumIndices(); }
        
        FUNCTION(Script)
        FORCEINLINE const FAABB& GetAABB() const { return BoundingBox; }
        
        template<typename TCallable>
        void ForEachSurface(TCallable&& Lambda) const;


        
        
        /** Material slots for this mesh — one entry per geometry surface. */
        PROPERTY(Editable, Category = "Materials")
        TVector<TObjectPtr<CMaterialInterface>> Materials;

        /** Axis-aligned bounding box computed from the mesh vertices. */
        PROPERTY(Script, Category = "AABB")
        FAABB BoundingBox;
        
    private:
        
        TUniquePtr<FMeshResource> MeshResources;
    };


    
    template <typename TCallable>
    void CMesh::ForEachSurface(TCallable&& Lambda) const
    {
        uint32 Count = 0;
        for (const FGeometrySurface& Surface : MeshResources->GeometrySurfaces)
        {
            std::forward<TCallable>(Lambda)(Surface, Count);
            ++Count;
        }
    }
}
