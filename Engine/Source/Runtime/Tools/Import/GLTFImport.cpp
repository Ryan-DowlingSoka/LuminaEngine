#include "pch.h"

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

            fastgltf::Parser Parser;
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
        
        // Texture extraction must run once per asset, NOT once per mesh. The
        // previous code nested this inside the mesh loop, so a glTF with N
        // meshes loaded the same image N times, prefixed each entry with the
        // current mesh name (making real deduplication impossible), and did
        // a CPU->GPU upload per copy for the dialog preview.
        if (ImportOptions.bImportTextures)
        {
            uint32 ImageCounter = 0;
            for (auto& Image : Asset.images)
            {
                FMeshImportImage GLTFImage;

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

        THashSet<FString> SeenMeshes;
        for (const fastgltf::Mesh& Mesh : Asset.meshes)
        {
            FString SanitizedMeshName = Mesh.name.c_str();
            eastl::replace(SanitizedMeshName.begin(), SanitizedMeshName.end(), '.', '_');

            auto It = SeenMeshes.find(SanitizedMeshName.c_str());
            if (It != SeenMeshes.end())
            {
                continue;
            }

            SeenMeshes.emplace(SanitizedMeshName);

            FFixedString MeshName;
            if (Mesh.name.empty())
            {
                MeshName.append(Name.begin(), Name.end()).append_convert(eastl::to_string(ImportData.Resources.size()));
            }
            else
            {
                MeshName.append_convert(SanitizedMeshName);
            }

            auto StaticMesh = MakeUnique<FMeshResource>();
            StaticMesh->Vertices = TVector<FVertex>();
            StaticMesh->Name = FString(MeshName) + "_Mesh";

            auto SkinnedMesh = MakeUnique<FMeshResource>();
            SkinnedMesh->Vertices = TVector<FSkinnedVertex>();
            SkinnedMesh->bSkinnedMesh = true;
            SkinnedMesh->Name = FString(MeshName) + "_SkeletalMesh";

            FMeshResource* NewResource = nullptr;
            for (auto& Primitive : Mesh.primitives)
            {
                auto Joints = Primitive.findAttribute("JOINTS_0");
                auto Weights = Primitive.findAttribute("WEIGHTS_0");
                if (Joints != Primitive.attributes.end() && Weights != Primitive.attributes.end())
                {
                    NewResource = SkinnedMesh.get();
                }
                else
                {
                    NewResource = StaticMesh.get();
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
                }
                
                NewSurface.ID = PrimitiveName;
                
                if (Primitive.materialIndex.has_value())
                {
                    NewSurface.MaterialIndex = (int16)Primitive.materialIndex.value();
                }
                
                size_t InitialIndex = NewResource->GetNumIndices();
                size_t InitialVert = NewResource->GetNumVertices();
                size_t VertexCount = Asset.accessors[Primitive.findAttribute("POSITION")->second].count;

                eastl::visit([&](auto& Vector)
                {
                    Vector.resize(InitialVert + VertexCount);
                }, NewResource->Vertices);
                
                for (size_t i = InitialVert; i < NewResource->GetNumVertices(); ++i)
                {
                    NewResource->SetNormalAt(i, PackNormal(FViewVolume::UpAxis));
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
                    Value *= ImportScale;
                    NewResource->SetPositionAt(InitialVert + Index, Value);
                });
                
                const fastgltf::Accessor& IndexAccessor = Asset.accessors[Primitive.indicesAccessor.value()];
                NewResource->Indices.reserve(InitialIndex + IndexAccessor.count);

                fastgltf::iterateAccessor<uint32>(Asset, IndexAccessor, [&](uint32 Index)
                {
                    NewResource->Indices.push_back(InitialVert + Index);
                });
                
                auto Normals = Primitive.findAttribute("NORMAL");
                if (Normals != Primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(Asset, Asset.accessors[Normals->second], [&](glm::vec3 Value, size_t Index)
                    {
                        NewResource->SetNormalAt(InitialVert + Index, PackNormal(glm::normalize(Value)));
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
            
            // Optimize and emit every resource that actually received vertices.
            // Previously only the resource handled by the LAST primitive was
            // optimized, and the other (static/skinned) was shipped unmodified.
            auto FinalizeResource = [&](TUniquePtr<FMeshResource>& Resource)
            {
                if (!Resource || Resource->GetNumVertices() == 0)
                {
                    return;
                }

                if (ImportOptions.bOptimize)
                {
                    OptimizeNewlyImportedMesh(*Resource);
                }

                GenerateShadowBuffers(*Resource);
                GenerateMeshlets(*Resource);
                AnalyzeMeshStatistics(*Resource, ImportData.MeshStatistics);

                ImportData.Resources.push_back(eastl::move(Resource));
            };

            FinalizeResource(StaticMesh);
            FinalizeResource(SkinnedMesh);
        }

        return Move(ImportData);
    }
}
