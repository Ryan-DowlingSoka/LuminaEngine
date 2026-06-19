#pragma once


#include "MeshComponent.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Core/Math/AABB.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Memory/SmartPtr.h"
#include "DynamicMeshComponent.generated.h"

namespace Lumina
{
    // CPU-side staging the build API fills before Commit; defined in the .cpp so the component header
    // stays light. Held behind a shared pointer so the component itself stays small and trivially movable.
    struct FDynamicMeshBuildData;

    // A mesh built entirely from data at runtime (from C# or C++) rather than loaded from an asset. Fill the
    // vertex streams and indices, optionally carve out per-material sections, then Commit() to upload it. The
    // result renders through the normal meshlet pipeline, so it supports materials, shadows and culling like a
    // static mesh. Rebuild as often as you like (e.g. voxel chunks) - each Commit re-uploads the GPU buffers.
    REFLECT(Component, Category = "Rendering")
    struct RUNTIME_API CACHE_ALIGN SDynamicMeshComponent : SMeshComponent
    {
        GENERATED_BODY()

        // The render path resolves materials through this (override beats the built mesh's slot).
        CMaterialInterface* GetMaterialForSlot(size_t Slot) const;

        /** World-local bounds of the committed mesh (empty until the first Commit). */
        FUNCTION(Script)
        FAABB GetAABB() const;

        /** Declare a sub-range of the index buffer that draws with one material slot. Optional: with no
         *  sections, Commit() makes a single section covering every index on slot 0. */
        FUNCTION(Script)
        void AddSection(int32 MaterialSlot, int32 StartIndex, int32 IndexCount);

        /** Drop all staged data and the built mesh, returning the component to an empty state. */
        FUNCTION(Script)
        void ClearMesh();

        /** Finalize the staged data: generate meshlets/LODs and upload the GPU buffers. Returns false if
         *  there is nothing renderable (no positions or no indices). Call after setting the streams. */
        FUNCTION(Script)
        bool Commit();

        /** True once Commit() has produced a renderable mesh. */
        FUNCTION(Script)
        bool IsBuilt() const;

        /** Number of vertices in the staged (pre-Commit) or committed mesh. */
        FUNCTION(Script)
        int32 GetVertexCount() const;

        /** Number of triangles in the staged (pre-Commit) or committed mesh. */
        FUNCTION(Script)
        int32 GetTriangleCount() const;

        // Bulk stream setters (called by the C# span exports in DotNetDynamicMesh.cpp; not script-bound
        // directly because member functions can't take spans). Counts are element counts: positions/normals
        // are 3 floats/vertex, UVs 2, colors 4 (float) or 1 (packed RGBA8), indices 1.
        void SetPositionsData(const float* Data, int32 FloatCount);
        void SetNormalsData(const float* Data, int32 FloatCount);
        void SetUVsData(const float* Data, int32 FloatCount);
        void SetColorsFloatData(const float* Data, int32 FloatCount);
        void SetColorsPackedData(const uint32* Data, int32 Count);
        void SetIndicesData(const uint32* Data, int32 Count);

        // Runtime-built mesh; transient, never serialized. The TObjectPtr keeps it alive (refcounted).
        TObjectPtr<CStaticMesh> DynamicMesh;

    private:

        FDynamicMeshBuildData& EnsureBuildData();

        TSharedPtr<FDynamicMeshBuildData> BuildData;

        // Cached at Commit so the count getters stay valid after the CPU scratch streams are dropped on upload.
        int32 CommittedVertexCount   = 0;
        int32 CommittedTriangleCount = 0;
    };
}
