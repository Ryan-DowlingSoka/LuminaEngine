#include "pch.h"

#include <filesystem>
#include <meshoptimizer.h>
#include <fastgltf/base64.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include "ImportHelpers.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "FileSystem/FileSystem.h"
#include "Memory/Memory.h"
#include "Paths/Paths.h"
#include "Renderer/MeshData.h"
#include "Renderer/RendererUtils.h"
#include "Renderer/RenderResource.h"
#include "Renderer/Vertex.h"
#include "TaskSystem/TaskSystem.h"

namespace Lumina::Import::Mesh::GLTF
{
    namespace
    {
        TExpected<fastgltf::Asset, FString> ExtractAsset(FStringView InPath)
        {
            std::filesystem::path FSPath(InPath.begin(), InPath.end());
        
            fastgltf::GltfDataBuffer Buffer;

            if (!Buffer.loadFromFile(FSPath))
            {
                return TUnexpected(std::format("Failed to load glTF model with path: {0}. Aborting import.", FSPath.string()).c_str());
            }

            fastgltf::GltfType SourceType = fastgltf::determineGltfFileType(&Buffer);

            if (SourceType == fastgltf::GltfType::Invalid)
            {
                return TUnexpected(std::format("Failed to determine glTF file type with path: {0}. Aborting import.", FSPath.string()).c_str());
            }

            constexpr fastgltf::Options options = fastgltf::Options::DontRequireValidAssetMember 
            | fastgltf::Options::LoadGLBBuffers 
            | fastgltf::Options::LoadExternalBuffers 
            | fastgltf::Options::GenerateMeshIndices 
            | fastgltf::Options::DecomposeNodeMatrices;

            fastgltf::Expected<fastgltf::Asset> Asset(fastgltf::Error::None);

            constexpr fastgltf::Extensions extensions =
                  fastgltf::Extensions::KHR_texture_transform
                | fastgltf::Extensions::KHR_texture_basisu
                | fastgltf::Extensions::MSFT_texture_dds
                | fastgltf::Extensions::KHR_mesh_quantization
                | fastgltf::Extensions::EXT_meshopt_compression
                | fastgltf::Extensions::KHR_lights_punctual
                | fastgltf::Extensions::EXT_texture_webp
                | fastgltf::Extensions::KHR_materials_specular
                | fastgltf::Extensions::KHR_materials_ior
                | fastgltf::Extensions::KHR_materials_iridescence
                | fastgltf::Extensions::KHR_materials_volume
                | fastgltf::Extensions::KHR_materials_transmission
                | fastgltf::Extensions::KHR_materials_clearcoat
                | fastgltf::Extensions::KHR_materials_emissive_strength
                | fastgltf::Extensions::KHR_materials_sheen
                | fastgltf::Extensions::KHR_materials_unlit
                | fastgltf::Extensions::KHR_materials_anisotropy
                | fastgltf::Extensions::EXT_mesh_gpu_instancing
                | fastgltf::Extensions::MSFT_packing_normalRoughnessMetallic
                | fastgltf::Extensions::MSFT_packing_occlusionRoughnessMetallic
                | fastgltf::Extensions::KHR_materials_dispersion
                | fastgltf::Extensions::KHR_materials_variants;

            fastgltf::Parser Parser(extensions);
            if (SourceType == fastgltf::GltfType::glTF)
            {
                Asset = Parser.loadGltf(&Buffer, FSPath.parent_path(), options);
            }
            else if (SourceType == fastgltf::GltfType::GLB)
            {
                Asset = Parser.loadGltfBinary(&Buffer, FSPath.parent_path(), options);
            }

            if (const fastgltf::Error& Error = Asset.error(); Error != fastgltf::Error::None)
            {
                return TUnexpected(std::format("Failed to load asset source with path: {0}. [{1}]: {2} Aborting import.", FSPath.string(),
                fastgltf::getErrorName(Error), fastgltf::getErrorMessage(Error)).c_str());
            }

            return Move(Asset.get());
        }
    }

    TExpected<FMeshImportData, FString> ImportGLTF(const FMeshImportOptions& ImportOptions, FStringView FilePath)
    {
        TExpected<fastgltf::Asset, FString> ExpectedAsset = ExtractAsset(FilePath.data());
        if (ExpectedAsset.IsError())
        {
            return TUnexpected(ExpectedAsset.Error());
        }
        
        const fastgltf::Asset& Asset = ExpectedAsset.Value();
        float ImportScale = ImportOptions.Scale;
        
        FStringView Name = VFS::FileName(FilePath, true);
        
        FMeshImportData ImportData;
        ImportData.Resources.reserve(Asset.meshes.size());
        
        for (const fastgltf::Animation& Animation : Asset.animations)
        {
            TUniquePtr<FAnimationResource> AnimClip = MakeUnique<FAnimationResource>();
            AnimClip->Name = Animation.name.c_str();
            
            for (const fastgltf::AnimationChannel& Channel : Animation.channels)
            {
                FAnimationChannel AnimChannel;
                
                size_t NodeIndex = Channel.nodeIndex.value();
                const fastgltf::Node& Node = Asset.nodes[NodeIndex];
                AnimChannel.TargetBone = FName(Node.name.empty() ? ("Bone_" + eastl::to_string(NodeIndex)) : Node.name.c_str());
                
                if (Channel.path == fastgltf::AnimationPath::Translation)
                {
                    AnimChannel.TargetPath = FAnimationChannel::ETargetPath::Translation;
                }
                else if (Channel.path == fastgltf::AnimationPath::Rotation)
                {
                    AnimChannel.TargetPath = FAnimationChannel::ETargetPath::Rotation;
                }
                else if (Channel.path == fastgltf::AnimationPath::Scale)
                {
                    AnimChannel.TargetPath = FAnimationChannel::ETargetPath::Scale;
                }
                
                const auto& Sampler = Animation.samplers[Channel.samplerIndex];
                
                const auto& TimeAccessor = Asset.accessors[Sampler.inputAccessor];
                fastgltf::iterateAccessor<float>(Asset, TimeAccessor, [&](float time)
                {
                    AnimChannel.Timestamps.push_back(time);
                });
                
                const auto& ValueAccessor = Asset.accessors[Sampler.outputAccessor];
                if (Channel.path == fastgltf::AnimationPath::Translation || Channel.path == fastgltf::AnimationPath::Scale)
                {
                    fastgltf::iterateAccessor<glm::vec3>(Asset, ValueAccessor, [&](glm::vec3 Value)
                    {
                        if (Channel.path == fastgltf::AnimationPath::Translation)
                        {
                            Value *= ImportScale;
                            AnimChannel.Translations.push_back(Value);
                        }
                        else
                        {
                            AnimChannel.Scales.push_back(Value);
                        }
                    });
                }
                else if (Channel.path == fastgltf::AnimationPath::Rotation)
                {
                    fastgltf::iterateAccessor<glm::vec4>(Asset, ValueAccessor, [&](glm::vec4 value)
                    {
                        AnimChannel.Rotations.push_back(glm::quat(value.w, value.x, value.y, value.z));
                    });
                }
                
                AnimClip->Channels.push_back(AnimChannel);
                AnimClip->Duration = glm::max(AnimClip->Duration, AnimChannel.Timestamps.back());
            }
            
            ImportData.Animations.push_back(Move(AnimClip));
        }
        
        for (const fastgltf::Skin& Skin : Asset.skins)
        {
            TUniquePtr<FSkeletonResource> NewSkeleton = MakeUnique<FSkeletonResource>();
    
            if (Skin.name.empty())
            {
                NewSkeleton->Name = FName("Skeleton_" + eastl::to_string(ImportData.Skeletons.size()));
            }
            else
            {
                NewSkeleton->Name = FName(Skin.name.c_str());
            }
            
            NewSkeleton->Bones.reserve(Skin.joints.size());
            
            TVector<glm::mat4> InverseBindMatrices;
            if (Skin.inverseBindMatrices.has_value())
            {
                const fastgltf::Accessor& MatrixAccessor = Asset.accessors[Skin.inverseBindMatrices.value()];
                InverseBindMatrices.reserve(MatrixAccessor.count);
                
                fastgltf::iterateAccessor<glm::mat4>(Asset, MatrixAccessor, [&](const glm::mat4& matrix)
                {
                    InverseBindMatrices.push_back(matrix);
                });
            }
            
            THashMap<size_t, size_t> NodeToParent;
            for (size_t nodeIdx = 0; nodeIdx < Asset.nodes.size(); ++nodeIdx)
            {
                const fastgltf::Node& Node = Asset.nodes[nodeIdx];
                for (size_t ChildIdx : Node.children)
                {
                    NodeToParent[ChildIdx] = nodeIdx;
                }
            }
            
            for (size_t JointIdx = 0; JointIdx < Skin.joints.size(); ++JointIdx)
            {
                size_t NodeIdx = Skin.joints[JointIdx];
                const fastgltf::Node& BoneNode = Asset.nodes[NodeIdx];
                
                FSkeletonResource::FBoneInfo Bone;
                Bone.Name = FName(BoneNode.name.empty() ? ("Bone_" + eastl::to_string(NodeIdx)) : BoneNode.name.c_str());
                
                Bone.ParentIndex = -1;
                
                auto ParentIt = NodeToParent.find(NodeIdx);
                if (ParentIt != NodeToParent.end())
                {
                    size_t ParentNodeIdx = ParentIt->second;
        
                    for (size_t i = 0; i < Skin.joints.size(); ++i)
                    {
                        if (Skin.joints[i] == ParentNodeIdx)
                        {
                            Bone.ParentIndex = (int32)i;
                            break;
                        }
                    }
                }
                
                glm::mat4 LocalTransform(1.0f);
                if (auto* trs = std::get_if<fastgltf::TRS>(&BoneNode.transform))
                {
                    glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(trs->translation[0], trs->translation[1], trs->translation[2]));
                    glm::quat rotation(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]);
                    glm::mat4 rotationMat = glm::mat4_cast(rotation);
                    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(trs->scale[0], trs->scale[1], trs->scale[2]));
                    LocalTransform = translation * rotationMat * scale;
                }
                else if (auto* mat = std::get_if<fastgltf::Node::TransformMatrix>(&BoneNode.transform))
                {
                    LocalTransform = glm::make_mat4(mat->data());
                }
                
                Bone.LocalTransform = LocalTransform;
                
                if (JointIdx < InverseBindMatrices.size())
                {
                    Bone.InvBindMatrix = InverseBindMatrices[JointIdx];
                }
                else
                {
                    Bone.InvBindMatrix = glm::mat4(1.0f);
                }
                
                NewSkeleton->Bones.push_back(Bone);
                NewSkeleton->BoneNameToIndex[Bone.Name] = (int32)JointIdx;
            }
            
            ImportData.Skeletons.push_back(Move(NewSkeleton));
        }
        
        // Texture extraction is per-asset; tag each image with its material role so the texture factory picks correct BC/colorspace.
        if (ImportOptions.bImportTextures)
        {
            TVector<ETextureColorSpace> ImageRoles(Asset.images.size(), ETextureColorSpace::Auto);

            auto MarkImageForTexture = [&](size_t TextureIndex, ETextureColorSpace Role)
            {
                if (TextureIndex >= Asset.textures.size()) return;
                const auto& Tex = Asset.textures[TextureIndex];
                if (!Tex.imageIndex.has_value()) return;
                const size_t ImgIdx = Tex.imageIndex.value();
                if (ImgIdx >= ImageRoles.size()) return;
                ImageRoles[ImgIdx] = Role;
            };

            for (const fastgltf::Material& Material : Asset.materials)
            {
                if (Material.pbrData.baseColorTexture.has_value())
                {
                    MarkImageForTexture(Material.pbrData.baseColorTexture->textureIndex, ETextureColorSpace::SRGB);
                }
                if (Material.pbrData.metallicRoughnessTexture.has_value())
                {
                    MarkImageForTexture(Material.pbrData.metallicRoughnessTexture->textureIndex, ETextureColorSpace::PackedData);
                }
                if (Material.normalTexture.has_value())
                {
                    MarkImageForTexture(Material.normalTexture->textureIndex, ETextureColorSpace::NormalMap);
                }
                if (Material.occlusionTexture.has_value())
                {
                    MarkImageForTexture(Material.occlusionTexture->textureIndex, ETextureColorSpace::Linear);
                }
                if (Material.emissiveTexture.has_value())
                {
                    MarkImageForTexture(Material.emissiveTexture->textureIndex, ETextureColorSpace::SRGB);
                }
            }

            uint32 ImageCounter = 0;
            for (auto& Image : Asset.images)
            {
                FMeshImportImage GLTFImage;
                GLTFImage.IntendedColorSpace = ImageRoles[ImageCounter];

                auto AssignFallbackName = [&]()
                {
                    if (!Image.name.empty())
                    {
                        GLTFImage.RelativePath = Image.name.c_str();
                    }
                    else
                    {
                        GLTFImage.RelativePath.append(Name.begin(), Name.end())
                                              .append("_Image_")
                                              .append_convert(eastl::to_string(ImageCounter));
                    }
                };

                if (auto* URI = std::get_if<fastgltf::sources::URI>(&Image.data))
                {
                    GLTFImage.RelativePath = URI->uri.c_str();
                    FFixedString FullPath = Paths::Combine(VFS::Parent(FilePath), GLTFImage.RelativePath);
                    GLTFImage.DisplayImage = Textures::CreateTextureFromImport(FullPath, true, glm::uvec2(128, 128));
                    ImportData.Textures.emplace(Move(GLTFImage));
                }
                else if (auto* BufferView = std::get_if<fastgltf::sources::BufferView>(&Image.data))
                {
                    const fastgltf::BufferView& View = Asset.bufferViews[BufferView->bufferViewIndex];
                    const fastgltf::Buffer& Buffer   = Asset.buffers[View.bufferIndex];

                    if (auto* BufferArray = std::get_if<fastgltf::sources::Array>(&Buffer.data))
                    {
                        const uint8* Start = BufferArray->bytes.data() + View.byteOffset;
                        GLTFImage.Bytes.assign(Start, Start + View.byteLength);
                    }

                    AssignFallbackName();
                    GLTFImage.DisplayImage = RenderUtils::CreateImageFromPixels(GLTFImage.Bytes, true, glm::uvec2(128, 128));
                    ImportData.Textures.emplace(Move(GLTFImage));
                }
                else if (auto* Array = std::get_if<fastgltf::sources::Array>(&Image.data))
                {
                    const uint8* Start = Array->bytes.data();
                    GLTFImage.Bytes.assign(Start, Start + Array->bytes.size());
                    AssignFallbackName();
                    GLTFImage.DisplayImage = RenderUtils::CreateImageFromPixels(GLTFImage.Bytes, true, glm::uvec2(128, 128));
                    ImportData.Textures.emplace(Move(GLTFImage));
                }
                else if (auto* Vector = std::get_if<fastgltf::sources::Vector>(&Image.data))
                {
                    const uint8* Start = Vector->bytes.data();
                    GLTFImage.Bytes.assign(Start, Start + Vector->bytes.size());
                    AssignFallbackName();
                    GLTFImage.DisplayImage = RenderUtils::CreateImageFromPixels(GLTFImage.Bytes, true, glm::uvec2(128, 128));
                    ImportData.Textures.emplace(Move(GLTFImage));
                }
                else if (auto* ByteView = std::get_if<fastgltf::sources::ByteView>(&Image.data))
                {
                    const uint8* Start = reinterpret_cast<const uint8*>(ByteView->bytes.data());
                    GLTFImage.Bytes.assign(Start, Start + ByteView->bytes.size());
                    AssignFallbackName();
                    GLTFImage.DisplayImage = RenderUtils::CreateImageFromPixels(GLTFImage.Bytes, true, glm::uvec2(128, 128));
                    ImportData.Textures.emplace(Move(GLTFImage));
                }

                ImageCounter++;
            }
        }

        auto NodeLocalMatrix = [](const fastgltf::Node& Node) -> glm::mat4
        {
            if (auto* Trs = std::get_if<fastgltf::TRS>(&Node.transform))
            {
                glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(Trs->translation[0], Trs->translation[1], Trs->translation[2]));
                glm::quat R(Trs->rotation[3], Trs->rotation[0], Trs->rotation[1], Trs->rotation[2]);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(Trs->scale[0], Trs->scale[1], Trs->scale[2]));
                return T * glm::mat4_cast(R) * S;
            }
            if (auto* Mat = std::get_if<fastgltf::Node::TransformMatrix>(&Node.transform))
            {
                return glm::make_mat4(Mat->data());
            }
            return glm::mat4(1.0f);
        };

        // Per-mesh primitive extraction; concurrent across resources but serial in merge mode (shared targets).
        auto ProcessMeshPrimitives = [&](
            const fastgltf::Mesh& Mesh,
            const FFixedString&   MeshName,
            FMeshResource*        StaticTarget,
            FMeshResource*        SkinnedTarget,
            THashMap<int16, int16>* MergedMaterialRemapPtr,
            const glm::mat4&      WorldMatrix)
        {
            const glm::mat4 PosMatrix    = glm::scale(glm::mat4(1.0f), glm::vec3(ImportScale)) * WorldMatrix;
            const glm::mat3 NormalMatrix = glm::transpose(glm::inverse(glm::mat3(WorldMatrix)));
            FMeshResource* NewResource = nullptr;
            for (auto& Primitive : Mesh.primitives)
            {
                auto Joints = Primitive.findAttribute("JOINTS_0");
                auto Weights = Primitive.findAttribute("WEIGHTS_0");
                if (Joints != Primitive.attributes.end() && Weights != Primitive.attributes.end())
                {
                    NewResource = SkinnedTarget;
                }
                else
                {
                    NewResource = StaticTarget;
                }

                FGeometrySurface NewSurface;
                NewSurface.StartIndex = (uint32)NewResource->GetNumIndices();

                FFixedString PrimitiveName;
                if (Mesh.name.empty())
                {
                    PrimitiveName.append(Name.begin(), Name.end()).append_convert(eastl::to_string(NewResource->GetNumSurfaces()));
                }
                else
                {
                    PrimitiveName.append_convert(Mesh.name);
                    if (MergedMaterialRemapPtr != nullptr)
                    {
                        PrimitiveName.append("_");
                        PrimitiveName.append_convert(eastl::to_string(NewResource->GetNumSurfaces()));
                    }
                }

                NewSurface.ID = PrimitiveName;

                if (Primitive.materialIndex.has_value())
                {
                    const int16 SourceMaterialIndex = (int16)Primitive.materialIndex.value();
                    if (MergedMaterialRemapPtr != nullptr)
                    {
                        THashMap<int16, int16>& Remap = *MergedMaterialRemapPtr;
                        auto MatIt = Remap.find(SourceMaterialIndex);
                        if (MatIt == Remap.end())
                        {
                            const int16 NewSlot = (int16)Remap.size();
                            Remap.emplace(SourceMaterialIndex, NewSlot);
                            NewSurface.MaterialIndex = NewSlot;
                        }
                        else
                        {
                            NewSurface.MaterialIndex = MatIt->second;
                        }
                    }
                    else
                    {
                        NewSurface.MaterialIndex = SourceMaterialIndex;
                    }
                }

                size_t InitialIndex = NewResource->GetNumIndices();
                size_t InitialVert = NewResource->GetNumVertices();
                size_t VertexCount = Asset.accessors[Primitive.findAttribute("POSITION")->second].count;

                eastl::visit([&](auto& Vector)
                {
                    Vector.resize(InitialVert + VertexCount);
                }, NewResource->Vertices);

                const glm::vec3 DefaultNormal = glm::normalize(NormalMatrix * FViewVolume::UpAxis);
                for (size_t i = InitialVert; i < NewResource->GetNumVertices(); ++i)
                {
                    NewResource->SetNormalAt(i, PackNormal(DefaultNormal));
                    NewResource->SetTangentAt(i, 0);
                    NewResource->SetUVAt(i, glm::u16vec2(0, 0));
                    NewResource->SetColorAt(i, 0xFFFFFFFF);
                    if (NewResource->IsSkinnedMesh())
                    {
                        NewResource->SetJointIndicesAt(i, glm::u8vec4(0));
                        NewResource->SetJointWeightsAt(i, glm::u8vec4(0));
                    }
                }

                const fastgltf::Accessor& PosAccessor = Asset.accessors[Primitive.findAttribute("POSITION")->second];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(Asset, PosAccessor, [&](glm::vec3 Value, size_t Index)
                {
                    glm::vec3 P = glm::vec3(PosMatrix * glm::vec4(Value, 1.0f));
                    NewResource->SetPositionAt(InitialVert + Index, P);
                });

                const fastgltf::Accessor& IndexAccessor = Asset.accessors[Primitive.indicesAccessor.value()];
                NewResource->Indices.reserve(InitialIndex + IndexAccessor.count);

                fastgltf::iterateAccessor<uint32>(Asset, IndexAccessor, [&](uint32 Index)
                {
                    NewResource->Indices.push_back((uint32)(InitialVert + Index));
                });

                auto Normals = Primitive.findAttribute("NORMAL");
                if (Normals != Primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(Asset, Asset.accessors[Normals->second], [&](glm::vec3 Value, size_t Index)
                    {
                        glm::vec3 N = glm::normalize(NormalMatrix * Value);
                        NewResource->SetNormalAt(InitialVert + Index, PackNormal(N));
                    });
                }

                auto UV = Primitive.findAttribute("TEXCOORD_0");
                if (UV != Primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(Asset, Asset.accessors[UV->second], [&](glm::vec2 Value, size_t Index)
                    {
                        if (ImportOptions.bFlipUVs)
                        {
                            Value.y = 1.0f - Value.y;
                        }

                        NewResource->SetUVAt(InitialVert + Index, Value);
                    });
                }

                auto Colors = Primitive.findAttribute("COLOR_0");
                if (Colors != Primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(Asset, Asset.accessors[Colors->second], [&](glm::vec4 Value, size_t Index)
                    {
                        NewResource->SetColorAt(InitialVert + Index, PackColor(Value));
                    });
                }

                if (Joints != Primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::u8vec4>(Asset, Asset.accessors[Joints->second], [&](glm::u8vec4 Value, size_t Index)
                    {
                        NewResource->SetJointIndicesAt(InitialVert + Index, Value);
                    });
                }

                if (Weights != Primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(Asset, Asset.accessors[Weights->second], [&](glm::vec4 Value, size_t Index)
                    {
                        NewResource->SetJointWeightsAt(InitialVert + Index, glm::u8vec4(Value * 255.0f));
                    });
                }

                NewSurface.IndexCount = (uint32)NewResource->GetNumIndices() - NewSurface.StartIndex;

                NewResource->GeometrySurfaces.push_back(NewSurface);
            }
        };

        // bSkipFinalization defers heavy passes to FinalizeMeshImportData at commit time.
        const bool bSkipFinalize = ImportOptions.bSkipFinalization;
        auto FinalizeResource = [&](FMeshResource& Resource)
        {
            if (bSkipFinalize)
            {
                return;
            }
            if (ImportOptions.bOptimize)
            {
                OptimizeNewlyImportedMesh(Resource);
            }
            GenerateMeshlets(Resource);
        };

        if (ImportOptions.bMergeMeshes)
        {
            // Merge mode: collapse all meshes into one static/skinned pair, deduping material slots.
            TUniquePtr<FMeshResource> MergedStaticMesh = MakeUnique<FMeshResource>();
            MergedStaticMesh->Vertices = TVector<FVertex>();
            MergedStaticMesh->Name = FString(Name.begin(), Name.end()) + "_Mesh";

            TUniquePtr<FMeshResource> MergedSkinnedMesh = MakeUnique<FMeshResource>();
            MergedSkinnedMesh->Vertices = TVector<FSkinnedVertex>();
            MergedSkinnedMesh->bSkinnedMesh = true;
            MergedSkinnedMesh->Name = FString(Name.begin(), Name.end()) + "_SkeletalMesh";

            THashMap<int16, int16> MergedMaterialRemap;

            // Walk the scene graph (not Asset.meshes) to accumulate node->world transforms; required for correct placement.
            auto VisitMeshInstance = [&](size_t MeshIdx, const glm::mat4& WorldMatrix)
            {
                const fastgltf::Mesh& Mesh = Asset.meshes[MeshIdx];

                FString SanitizedMeshName = Mesh.name.c_str();
                eastl::replace(SanitizedMeshName.begin(), SanitizedMeshName.end(), '.', '_');

                FFixedString MeshName;
                if (Mesh.name.empty())
                {
                    MeshName.append(Name.begin(), Name.end()).append_convert(eastl::to_string(ImportData.Resources.size()));
                }
                else
                {
                    MeshName.append_convert(SanitizedMeshName);
                }

                ProcessMeshPrimitives(Mesh, MeshName, MergedStaticMesh.get(), MergedSkinnedMesh.get(), &MergedMaterialRemap, WorldMatrix);
            };

            const size_t SceneIdx = Asset.defaultScene.has_value() ? Asset.defaultScene.value()
                                                                   : (Asset.scenes.empty() ? size_t(-1) : 0);

            if (SceneIdx != size_t(-1))
            {
                struct FStackEntry
                {
                    size_t    NodeIdx;
                    glm::mat4 ParentWorld;
                };
                TVector<FStackEntry> Stack;
                for (size_t Root : Asset.scenes[SceneIdx].nodeIndices)
                {
                    Stack.push_back({Root, glm::mat4(1.0f)});
                }

                while (!Stack.empty())
                {
                    FStackEntry Entry = Stack.back();
                    Stack.pop_back();

                    const fastgltf::Node& Node = Asset.nodes[Entry.NodeIdx];
                    glm::mat4 World = Entry.ParentWorld * NodeLocalMatrix(Node);

                    if (Node.meshIndex.has_value())
                    {
                        VisitMeshInstance(Node.meshIndex.value(), World);
                    }

                    for (size_t Child : Node.children)
                    {
                        Stack.push_back({Child, World});
                    }
                }
            }
            else
            {
                // No scene info; fall back to identity-transform iteration.
                for (size_t MeshIdx = 0; MeshIdx < Asset.meshes.size(); ++MeshIdx)
                {
                    VisitMeshInstance(MeshIdx, glm::mat4(1.0f));
                }
            }

            TVector<FMeshResource*> ToFinalize;
            if (MergedStaticMesh && MergedStaticMesh->GetNumVertices() > 0)
            {
                ToFinalize.push_back(MergedStaticMesh.get());
            }
            if (MergedSkinnedMesh && MergedSkinnedMesh->GetNumVertices() > 0)
            {
                ToFinalize.push_back(MergedSkinnedMesh.get());
            }

            Task::ParallelFor((uint32)ToFinalize.size(), [&](uint32 i)
            {
                FinalizeResource(*ToFinalize[i]);
            });

            for (FMeshResource* Res : ToFinalize)
            {
                AnalyzeMeshStatistics(*Res, ImportData.MeshStatistics);
            }

            if (MergedStaticMesh && MergedStaticMesh->GetNumVertices() > 0)
            {
                ImportData.Resources.push_back(eastl::move(MergedStaticMesh));
            }
            if (MergedSkinnedMesh && MergedSkinnedMesh->GetNumVertices() > 0)
            {
                ImportData.Resources.push_back(eastl::move(MergedSkinnedMesh));
            }
        }
        else
        {
            // Non-merge: one resource per node->mesh reference; ImportTransform carries node world placement for commit-time merge.
            struct FInstance
            {
                size_t       MeshIdx;
                glm::mat4    World;
                FFixedString MeshName;
            };

            TVector<FInstance> Instances;
            THashMap<size_t, uint32> InstanceCountPerMesh;

            auto EmitInstance = [&](size_t MeshIdx, const glm::mat4& World)
            {
                const fastgltf::Mesh& Mesh = Asset.meshes[MeshIdx];

                FString SanitizedMeshName = Mesh.name.c_str();
                eastl::replace(SanitizedMeshName.begin(), SanitizedMeshName.end(), '.', '_');

                FFixedString MeshName;
                if (Mesh.name.empty())
                {
                    MeshName.append(Name.begin(), Name.end()).append_convert(eastl::to_string(MeshIdx));
                }
                else
                {
                    MeshName.append_convert(SanitizedMeshName);
                }

                // Disambiguate same-mesh-multi-node so asset names don't collide.
                uint32& Count = InstanceCountPerMesh[MeshIdx];
                if (Count > 0)
                {
                    MeshName.append("_");
                    MeshName.append_convert(eastl::to_string(Count));
                }
                ++Count;

                Instances.push_back({MeshIdx, World, MeshName});
            };

            const size_t SceneIdx = Asset.defaultScene.has_value() ? Asset.defaultScene.value()
                                                                   : (Asset.scenes.empty() ? size_t(-1) : 0);

            if (SceneIdx != size_t(-1))
            {
                struct FStackEntry
                {
                    size_t    NodeIdx;
                    glm::mat4 ParentWorld;
                };
                TVector<FStackEntry> Stack;
                for (size_t Root : Asset.scenes[SceneIdx].nodeIndices)
                {
                    Stack.push_back({Root, glm::mat4(1.0f)});
                }

                while (!Stack.empty())
                {
                    FStackEntry Entry = Stack.back();
                    Stack.pop_back();

                    const fastgltf::Node& Node = Asset.nodes[Entry.NodeIdx];
                    glm::mat4 World = Entry.ParentWorld * NodeLocalMatrix(Node);

                    if (Node.meshIndex.has_value())
                    {
                        EmitInstance(Node.meshIndex.value(), World);
                    }

                    for (size_t Child : Node.children)
                    {
                        Stack.push_back({Child, World});
                    }
                }
            }
            else
            {
                // No scene info; fall back to one identity-transform instance per mesh.
                for (size_t MeshIdx = 0; MeshIdx < Asset.meshes.size(); ++MeshIdx)
                {
                    EmitInstance(MeshIdx, glm::mat4(1.0f));
                }
            }

            struct FMeshSlot
            {
                TUniquePtr<FMeshResource> Static;
                TUniquePtr<FMeshResource> Skinned;
            };

            TVector<FMeshSlot> Slots(Instances.size());

            // Phase 1: extract per-instance data in parallel; vertices stay mesh-local, ImportTransform carries placement.
            Task::ParallelFor((uint32)Instances.size(), [&](uint32 SlotIdx)
            {
                const FInstance&      Inst   = Instances[SlotIdx];
                const fastgltf::Mesh& Mesh   = Asset.meshes[Inst.MeshIdx];
                const FFixedString&   MeshNm = Inst.MeshName;

                FMeshSlot& Slot = Slots[SlotIdx];

                Slot.Static = MakeUnique<FMeshResource>();
                Slot.Static->Vertices = TVector<FVertex>();
                Slot.Static->Name = FString(MeshNm) + "_Mesh";
                Slot.Static->ImportTransform = Inst.World;

                Slot.Skinned = MakeUnique<FMeshResource>();
                Slot.Skinned->Vertices = TVector<FSkinnedVertex>();
                Slot.Skinned->bSkinnedMesh = true;
                Slot.Skinned->Name = FString(MeshNm) + "_SkeletalMesh";
                Slot.Skinned->ImportTransform = Inst.World;

                ProcessMeshPrimitives(Mesh, MeshNm, Slot.Static.get(), Slot.Skinned.get(), nullptr, glm::mat4(1.0f));
            });

            // Phase 2: finalize every non-empty resource in parallel; each resource touched by one task.
            TVector<FMeshResource*> ToFinalize;
            ToFinalize.reserve(Slots.size() * 2);
            for (FMeshSlot& Slot : Slots)
            {
                if (Slot.Static && Slot.Static->GetNumVertices() > 0)
                {
                    ToFinalize.push_back(Slot.Static.get());
                }
                if (Slot.Skinned && Slot.Skinned->GetNumVertices() > 0)
                {
                    ToFinalize.push_back(Slot.Skinned.get());
                }
            }

            Task::ParallelFor((uint32)ToFinalize.size(), [&](uint32 i)
            {
                FinalizeResource(*ToFinalize[i]);
            });

            // Phase 3: serial collect into ImportData (push_back + stats aren't threadsafe).
            for (FMeshSlot& Slot : Slots)
            {
                if (Slot.Static && Slot.Static->GetNumVertices() > 0)
                {
                    AnalyzeMeshStatistics(*Slot.Static, ImportData.MeshStatistics);
                    ImportData.Resources.push_back(eastl::move(Slot.Static));
                }
                if (Slot.Skinned && Slot.Skinned->GetNumVertices() > 0)
                {
                    AnalyzeMeshStatistics(*Slot.Skinned, ImportData.MeshStatistics);
                    ImportData.Resources.push_back(eastl::move(Slot.Skinned));
                }
            }
        }

        return Move(ImportData);
    }
}
