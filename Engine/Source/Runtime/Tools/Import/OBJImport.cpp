#include "PCH.h"
#include <glm/glm.hpp>
#include <tinyobjloader/tiny_obj_loader.h>
#include "ImportHelpers.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Renderer/MeshData.h"


namespace Lumina::Import::Mesh::OBJ
{
    TExpected<FMeshImportData, FString> ImportOBJ(const FMeshImportOptions& ImportOptions, FStringView FilePath)
    {
        tinyobj::ObjReaderConfig ReaderConfig;

        tinyobj::ObjReader Reader;

        if (!Reader.ParseFromFile(FilePath.data(), ReaderConfig))
        {
            if (!Reader.Error().empty())
            {
                LOG_ERROR("TinyObjReader Error: {}", Reader.Error());
            }

            return TUnexpected(Reader.Error().c_str());
        }

        if (!Reader.Warning().empty())
        {
            LOG_WARN("TinyObjReader Warning: {}", Reader.Warning());
        }
        
    
        const tinyobj::attrib_t& Attribute                  = Reader.GetAttrib();
        const std::vector<tinyobj::shape_t>& Shapes         = Reader.GetShapes();
        const std::vector<tinyobj::material_t>& Materials   = Reader.GetMaterials();

        FMeshImportData ImportData;
        
        TUniquePtr<FMeshResource> MeshResource = MakeUnique<FMeshResource>();
        MeshResource->Name = VFS::FileName(FilePath, true);

        if (ImportOptions.bImportTextures)
        {
            for (const tinyobj::material_t& Material : Materials)
            {
                if (!Material.diffuse_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.diffuse_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }
        
                if (!Material.bump_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.bump_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }
        
                if (!Material.specular_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.specular_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }

                if (!Material.ambient_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.ambient_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }

                if (!Material.specular_highlight_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.specular_highlight_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }

                if (!Material.metallic_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.metallic_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }

                if (!Material.roughness_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.roughness_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }

                if (!Material.emissive_texname.empty())
                {
                    FMeshImportImage Image;
                    //Image.ByteOffset = 0;
                    Image.RelativePath = Material.emissive_texname.c_str();
                    ImportData.Textures.emplace(Image);
                }
            }
        }
        
        bool bIsSkinned = !Attribute.skin_weights.empty();
        if (bIsSkinned)
        {
            MeshResource->Vertices = TVector<FSkinnedVertex>();
            MeshResource->bSkinnedMesh = true;
        }
        else
        {
            MeshResource->Vertices = TVector<FVertex>();
        }
        
        for (const tinyobj::shape_t& Shape : Shapes)
        {
            const size_t NumFaces = Shape.mesh.num_face_vertices.size();

            // Precompute the index offset for each face so we can iterate faces
            // in arbitrary order below (faces don't have to be material-sorted
            // in the OBJ).
            TVector<size_t> FaceIndexOffsets;
            FaceIndexOffsets.resize(NumFaces);
            {
                size_t Running = 0;
                for (size_t Face = 0; Face < NumFaces; ++Face)
                {
                    FaceIndexOffsets[Face] = Running;
                    Running += Shape.mesh.num_face_vertices[Face];
                }
            }

            // Bucket faces by material. Previous code created one surface per
            // shape and reassigned MaterialIndex inside the face loop, which
            // meant multi-material OBJs ended up with every face attributed to
            // the LAST face's material. Now each (shape, material) pair gets
            // its own surface, which is what the renderer's per-surface
            // material lookup expects.
            THashMap<int, TVector<size_t>> FacesByMaterial;
            for (size_t Face = 0; Face < NumFaces; ++Face)
            {
                const int MaterialID = Shape.mesh.material_ids.empty()
                    ? -1
                    : Shape.mesh.material_ids[Face];
                FacesByMaterial[MaterialID].push_back(Face);
            }

            for (auto& [MaterialID, FaceList] : FacesByMaterial)
            {
                FGeometrySurface& Surface = MeshResource->GeometrySurfaces.emplace_back();
                // Disambiguate surface IDs when the same shape splits across
                // multiple materials so the editor surface list stays useful.
                if (FacesByMaterial.size() > 1)
                {
                    FFixedString SurfaceID;
                    SurfaceID.append_convert(Shape.name).append("_Mat").append_convert(eastl::to_string(MaterialID));
                    Surface.ID = SurfaceID;
                }
                else
                {
                    Surface.ID = Shape.name.c_str();
                }
                Surface.MaterialIndex = (int16)MaterialID;
                Surface.IndexCount    = 0;
                Surface.StartIndex    = static_cast<uint32>(MeshResource->Indices.size());

                for (size_t Face : FaceList)
                {
                    const size_t NumFaceVerts = Shape.mesh.num_face_vertices[Face];
                    const size_t IndexOffset  = FaceIndexOffsets[Face];

                    for (size_t V = 0; V < NumFaceVerts; ++V)
                    {
                        tinyobj::index_t Index = Shape.mesh.indices[IndexOffset + V];

                        MeshResource->Indices.push_back(static_cast<uint32>(MeshResource->GetNumVertices()));
                        Surface.IndexCount++;

                        FSkinnedVertex Vertex;
                        Vertex.Position.x = Attribute.vertices[3 * Index.vertex_index + 0];
                        Vertex.Position.y = Attribute.vertices[3 * Index.vertex_index + 1];
                        Vertex.Position.z = Attribute.vertices[3 * Index.vertex_index + 2];

                        if (Index.normal_index >= 0)
                        {
                            glm::vec3 Normal;
                            Normal.x = Attribute.normals[3 * Index.normal_index + 0];
                            Normal.y = Attribute.normals[3 * Index.normal_index + 1];
                            Normal.z = Attribute.normals[3 * Index.normal_index + 2];
                            Vertex.Normal = PackNormal(glm::normalize(Normal));
                        }

                        if (Index.texcoord_index >= 0)
                        {
                            Vertex.UV = glm::packHalf2x16(glm::vec2(Attribute.texcoords[2 * Index.texcoord_index + 0], Attribute.texcoords[2 * Index.texcoord_index + 1]));
                        }

                        if (bIsSkinned)
                        {
                            eastl::get<TVector<FSkinnedVertex>>(MeshResource->Vertices).push_back(Vertex);
                        }
                        else
                        {
                            FVertex StaticVertex;
                            StaticVertex.Position = Vertex.Position;
                            StaticVertex.Normal   = Vertex.Normal;
                            StaticVertex.UV       = Vertex.UV;
                            StaticVertex.Color    = Vertex.Color;
                            eastl::get<TVector<FVertex>>(MeshResource->Vertices).push_back(StaticVertex);
                        }
                    }
                }
            }
        }
        
        // Skip the heavy passes when the dialog has asked for a raw preview
        // parse; FinalizeMeshImportData runs them at commit time.
        if (!ImportOptions.bSkipFinalization)
        {
            if (ImportOptions.bOptimize)
            {
                OptimizeNewlyImportedMesh(*MeshResource);
            }
            GenerateShadowBuffers(*MeshResource);
            GenerateMeshlets(*MeshResource);
        }
        AnalyzeMeshStatistics(*MeshResource, ImportData.MeshStatistics);

        ImportData.Resources.push_back(Move(MeshResource));
        
        return Move(ImportData);
    }
}

