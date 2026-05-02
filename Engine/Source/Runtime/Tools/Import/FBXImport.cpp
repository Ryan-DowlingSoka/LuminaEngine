#include "PCH.h"
#include <glm/gtx/string_cast.hpp>
#include "ImportHelpers.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Core/Utils/Defer.h"
#include "FileSystem/FileSystem.h"
#include "OpenFBX/ofbx.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Renderer/MeshData.h"
#include "TaskSystem/TaskSystem.h"

namespace Lumina::Import::Mesh::FBX
{
    static glm::mat4 ConvertMatrix(const ofbx::DMatrix& m)
    {
        return glm::mat4(
            m.m[0], m.m[1], m.m[2], m.m[3],
            m.m[4], m.m[5], m.m[6], m.m[7],
            m.m[8], m.m[9], m.m[10], m.m[11],
            m.m[12], m.m[13], m.m[14], m.m[15]
        );
    }
    
    
    TExpected<FMeshImportData, FString> ImportFBX(const FMeshImportOptions& ImportOptions, FStringView FilePath)
    {
        TVector<uint8> FileBlob;
        if (!FileHelper::LoadFileToArray(FileBlob, FilePath))
        {
            return TUnexpected("Failed to load file path");
        }
        
        ofbx::LoadFlags LoadFlags = ofbx::LoadFlags::IGNORE_CAMERAS | ofbx::LoadFlags::IGNORE_LIGHTS | ofbx::LoadFlags::IGNORE_VIDEOS;

        ofbx::IScene* FBXScene = ofbx::load(FileBlob.data(), FileBlob.size(), (uint16)LoadFlags);
        if (!FBXScene)
        {
            return TUnexpected("Failed to load FBX Scene");
        }
        
        float SceneScale = FBXScene->getGlobalSettings()->UnitScaleFactor * 0.01f;
        SceneScale *= ImportOptions.Scale;
        
        DEFER
        {
            FBXScene->destroy();
        };
        
        FMeshImportData ImportData;
        FStringView FileName = VFS::FileName(FilePath, true);
        
        
        int AnimStackCount = ImportOptions.bImportAnimations ? FBXScene->getAnimationStackCount() : 0;
        for (int i = 0; i < AnimStackCount; ++i)
        {
            const ofbx::AnimationStack* AnimationStack = FBXScene->getAnimationStack(i);
            auto AnimClip = MakeUnique<FAnimationResource>();
            AnimClip->Name = FString(AnimationStack->name) + "_Animation";
            
            const ofbx::AnimationLayer* Layer = AnimationStack->getLayer(0);
            if (Layer == nullptr)
            {
                continue;
            }
        
            if (const ofbx::TakeInfo* TakeInfo = FBXScene->getTakeInfo(AnimationStack->name))
            {
                AnimClip->Duration = static_cast<float>(TakeInfo->local_time_to - TakeInfo->local_time_from);
            }
            
            int ObjectCount = FBXScene->getAllObjectCount();
            for (int ObjIdx = 0; ObjIdx < ObjectCount; ++ObjIdx)
            {
                const ofbx::Object* Bone = FBXScene->getAllObjects()[ObjIdx];
                
                if (!Bone->isNode())
                {
                    continue;
                }
                
                const ofbx::AnimationCurveNode* TranslationNode = Layer->getCurveNode(*Bone, "Lcl Translation");
                const ofbx::AnimationCurveNode* RotationNode    = Layer->getCurveNode(*Bone, "Lcl Rotation");
                const ofbx::AnimationCurveNode* ScaleNode       = Layer->getCurveNode(*Bone, "Lcl Scale");
                
                if (!TranslationNode && !RotationNode && !ScaleNode)
                {
                    continue;
                }
        
                auto EvalCurve = [](const ofbx::AnimationCurve* Curve, int64 Time) -> double
                {
                    if (!Curve)
                    {
                        return 0.0;
                    }
        
                    int KeyCount = Curve->getKeyCount();
                    const int64* KeyTimes = Curve->getKeyTime();
                    const float* KeyValues = Curve->getKeyValue();
                    
                    for (int j = 0; j < KeyCount - 1; ++j)
                    {
                        if (Time >= KeyTimes[j] && Time <= KeyTimes[j + 1])
                        {
                            double t = (double)(Time - KeyTimes[j]) / (double)(KeyTimes[j + 1] - KeyTimes[j]);
                            return KeyValues[j] + t * (KeyValues[j + 1] - KeyValues[j]);
                        }
                    }
                    
                    if (Time < KeyTimes[0])
                    {
                        return KeyValues[0];
                    }
                    return KeyValues[KeyCount - 1];
                };
        
                TSet<int64> AllTimestamps;
                
                auto CollectTimestamps = [&](const ofbx::AnimationCurveNode* Node)
                {
                    if (!Node)
                    {
                        return;
                    }
                    for (int j = 0; j < 3; ++j)
                    {
                        if (const ofbx::AnimationCurve* Curve = Node->getCurve(j))
                        {
                            int KeyCount = Curve->getKeyCount();
                            const int64* KeyTimes = Curve->getKeyTime();
                            for (int k = 0; k < KeyCount; ++k)
                            {
                                AllTimestamps.insert(KeyTimes[k]);
                            }
                        }
                    }
                };
                
                CollectTimestamps(TranslationNode);
                CollectTimestamps(RotationNode);
                CollectTimestamps(ScaleNode);
                
                if (AllTimestamps.empty())
                {
                    continue;
                }
                
                TVector<int64> SortedTimestamps(AllTimestamps.begin(), AllTimestamps.end());
                eastl::sort(SortedTimestamps.begin(), SortedTimestamps.end());
                
                struct KeyframeData
                {
                    float Time;
                    glm::vec3 Translation;
                    glm::quat Rotation;
                    glm::vec3 Scale;
                };
                
                TVector<KeyframeData> Keyframes;
                Keyframes.reserve(SortedTimestamps.size());
                
                for (int64 FBXTime : SortedTimestamps)
                {
                    float Time = static_cast<float>(ofbx::fbxTimeToSeconds(FBXTime));
                    
                    ofbx::DVec3 AnimTranslation = Bone->getLocalTranslation();
                    ofbx::DVec3 AnimRotation = Bone->getLocalRotation();
                    ofbx::DVec3 AnimScale = Bone->getLocalScaling();
                    
                    if (TranslationNode) 
                    {
                        AnimTranslation.x = EvalCurve(TranslationNode->getCurve(0), FBXTime);
                        AnimTranslation.y = EvalCurve(TranslationNode->getCurve(1), FBXTime);
                        AnimTranslation.z = EvalCurve(TranslationNode->getCurve(2), FBXTime);
                    }
                    
                    if (RotationNode)
                    {
                        AnimRotation.x = EvalCurve(RotationNode->getCurve(0), FBXTime);
                        AnimRotation.y = EvalCurve(RotationNode->getCurve(1), FBXTime);
                        AnimRotation.z = EvalCurve(RotationNode->getCurve(2), FBXTime);
                    }
                    
                    if (ScaleNode) 
                    {
                        AnimScale.x = EvalCurve(ScaleNode->getCurve(0), FBXTime);
                        AnimScale.y = EvalCurve(ScaleNode->getCurve(1), FBXTime);
                        AnimScale.z = EvalCurve(ScaleNode->getCurve(2), FBXTime);
                    }
                    
                    ofbx::DMatrix LocalMatrix = Bone->evalLocal(AnimTranslation, AnimRotation, AnimScale);
                    glm::mat4 Mat = ConvertMatrix(LocalMatrix);
                    
                    Mat[3][0] *= SceneScale;
                    Mat[3][1] *= SceneScale;
                    Mat[3][2] *= SceneScale;
                    
                    KeyframeData Keyframe;
                    Keyframe.Time = Time;
                    Keyframe.Translation = glm::vec3(Mat[3][0], Mat[3][1], Mat[3][2]);
                    Keyframe.Rotation = glm::quat_cast(Mat);
                    Keyframe.Scale = glm::vec3(glm::length(glm::vec3(Mat[0])), glm::length(glm::vec3(Mat[1])), glm::length(glm::vec3(Mat[2])));
                    
                    Keyframes.push_back(Keyframe);
                }
                
                if (TranslationNode)
                {
                    FAnimationChannel TranslationChannel;
                    TranslationChannel.TargetBone = Bone->name;
                    TranslationChannel.TargetPath = FAnimationChannel::ETargetPath::Translation;
                    
                    for (const KeyframeData& KF : Keyframes)
                    {
                        TranslationChannel.Timestamps.push_back(KF.Time);
                        TranslationChannel.Translations.push_back(KF.Translation);
                    }
                    
                    AnimClip->Channels.push_back(TranslationChannel);
                }
                
                if (RotationNode)
                {
                    FAnimationChannel RotationChannel;
                    RotationChannel.TargetBone = Bone->name;
                    RotationChannel.TargetPath = FAnimationChannel::ETargetPath::Rotation;
                    
                    for (const KeyframeData& KF : Keyframes)
                    {
                        RotationChannel.Timestamps.push_back(KF.Time);
                        RotationChannel.Rotations.push_back(KF.Rotation);
                    }
                    
                    AnimClip->Channels.push_back(RotationChannel);
                }
                
                if (ScaleNode)
                {
                    FAnimationChannel ScaleChannel;
                    ScaleChannel.TargetBone = Bone->name;
                    ScaleChannel.TargetPath = FAnimationChannel::ETargetPath::Scale;
                    
                    for (const KeyframeData& KF : Keyframes)
                    {
                        ScaleChannel.Timestamps.push_back(KF.Time);
                        ScaleChannel.Scales.push_back(KF.Scale);
                    }
                    
                    AnimClip->Channels.push_back(ScaleChannel);
                }
            }
            
            if (!AnimClip->Channels.empty())
            {
                ImportData.Animations.push_back(Move(AnimClip));
            }
        }
        
        int DataCount = FBXScene->getEmbeddedDataCount();
        
        for (int DataIdx = 0; DataIdx < DataCount; ++DataIdx)
        {
            ofbx::DataView Data = FBXScene->getEmbeddedData(DataIdx);
            auto Filename = FBXScene->getEmbeddedFilename(DataIdx);
            
        }
        
        
        
        THashMap<FName, int32> BoneNameToIndex;
        TVector<const ofbx::Object*> BoneObjects;

        int MeshCount = FBXScene->getMeshCount();

        for (int MeshIdx = 0; MeshIdx < MeshCount; ++MeshIdx)
        {
            const ofbx::Mesh* Mesh = FBXScene->getMesh(MeshIdx);
            int MaterialCount = Mesh->getMaterialCount();

            for (int MatIdx = 0; MatIdx < MaterialCount; ++MatIdx)
            {
                const ofbx::Material* Material = Mesh->getMaterial(MatIdx);
                auto Texture = Material->getTexture(ofbx::Texture::DIFFUSE);
                
                if (Texture)
                {
                    ofbx::DataView View = Texture->getEmbeddedData(); 
                    if (View.begin != View.end) 
                    {
                        
                    }
                    else
                    {
                        ofbx::DataView filename = Texture->getRelativeFileName();
                        ofbx::DataView absolutePath = Texture->getFileName();
                        FString Path((FString::value_type*)absolutePath.begin, (FString::value_type*)absolutePath.end);
                    }
                }
            }
        }
        
        for (int MeshIdx = 0; MeshIdx < MeshCount; ++MeshIdx)
        {
            const ofbx::Mesh* Mesh = FBXScene->getMesh(MeshIdx);
            const ofbx::Skin* Skin = Mesh->getSkin();

            if (!Skin)
            {
                continue;
            }

            int ClusterCount = Skin->getClusterCount();
            for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
            {
                const ofbx::Cluster* Cluster = Skin->getCluster(ClusterIdx);
                const ofbx::Object* JointNode = Cluster->getLink();
                
                if (BoneNameToIndex.find(JointNode->name) == BoneNameToIndex.end())
                {
                    int32 BoneIndex = static_cast<int32>(BoneObjects.size());
                    BoneObjects.push_back(JointNode);
                    BoneNameToIndex[JointNode->name] = BoneIndex;
                }
            }
        }
        
        if (!BoneObjects.empty() && ImportOptions.bImportSkeleton)
        {
            auto Skeleton = MakeUnique<FSkeletonResource>();
            Skeleton->Bones.resize(BoneObjects.size());
            Skeleton->Name = FString(FileName) + "_Skeleton";
            
            for (int MeshIdx = 0; MeshIdx < MeshCount; ++MeshIdx)
            {
                const ofbx::Mesh* Mesh = FBXScene->getMesh(MeshIdx);
                const ofbx::Skin* Skin = Mesh->getSkin();
    
                if (!Skin)
                {
                    continue;
                }
    
                int ClusterCount = Skin->getClusterCount();
                for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
                {
                    const ofbx::Cluster* Cluster = Skin->getCluster(ClusterIdx);
                    const ofbx::Object* JointNode = Cluster->getLink();
        
                    if (!JointNode)
                    {
                        continue;
                    }
        
                    auto It = BoneNameToIndex.find(JointNode->name);
                    if (It == BoneNameToIndex.end())
                    {
                        continue;
                    }
        
                    int32 BoneIndex = It->second;
                    
                    ofbx::DMatrix TransformLinkMatrix = Cluster->getTransformLinkMatrix();
                    glm::mat4 JointWorldTransform = ConvertMatrix(TransformLinkMatrix);
                    
                    JointWorldTransform[3][0] *= SceneScale;
                    JointWorldTransform[3][1] *= SceneScale;
                    JointWorldTransform[3][2] *= SceneScale;
                    
                    Skeleton->Bones[BoneIndex].InvBindMatrix = glm::inverse(JointWorldTransform);
                }
            }
        
            
            for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(BoneObjects.size()); ++BoneIndex)
            {
                const ofbx::Object* BoneObj = BoneObjects[BoneIndex];
    
                FSkeletonResource::FBoneInfo& BoneInfo = Skeleton->Bones[BoneIndex];
                BoneInfo.Name = BoneObj->name;
    
                const ofbx::Object* ParentObj = BoneObj->getParent();
                BoneInfo.ParentIndex = INDEX_NONE;
    
                if (ParentObj)
                {
                    auto It = BoneNameToIndex.find(ParentObj->name);
                    if (It != BoneNameToIndex.end())
                    {
                        BoneInfo.ParentIndex = It->second;
                    }
                }
    
                ofbx::DMatrix LocalMatrix = BoneObj->getLocalTransform();
                glm::mat4 LocalTransform = ConvertMatrix(LocalMatrix);
                
                LocalTransform[3][0] *= SceneScale;
                LocalTransform[3][1] *= SceneScale;
                LocalTransform[3][2] *= SceneScale;
                
                BoneInfo.LocalTransform = LocalTransform;
            }
            
            Skeleton->BoneNameToIndex = BoneNameToIndex;
            ImportData.Skeletons.push_back(Move(Skeleton));
        }
        
        
        ImportData.Resources.reserve(MeshCount);

        // Per-mesh extraction in parallel into FFBXMeshResult; serial merge concatenates with rebased offsets.
        struct FFBXMeshResult
        {
            bool                        bSkinned = false;
            TVector<FVertex>            StaticVerts;
            TVector<FSkinnedVertex>     SkinnedVerts;
            TVector<uint32>             Indices;
            TVector<FGeometrySurface>   Surfaces;
        };

        TVector<FFBXMeshResult> Results(MeshCount);

        Task::ParallelFor((uint32)MeshCount, [&](uint32 MeshIdx)
        {
            const ofbx::Mesh* Mesh = FBXScene->getMesh((int)MeshIdx);
            const ofbx::Skin* OFBXSkin = Mesh->getSkin();
            const bool bSkinned = OFBXSkin != nullptr;

            FFBXMeshResult& Result = Results[MeshIdx];
            Result.bSkinned = bSkinned;

            THashMap<int, uint32> VertexIndexMap;

            const ofbx::GeometryData&    Geometry  = Mesh->getGeometryData();
            const ofbx::Vec3Attributes   Positions = Geometry.getPositions();
            const ofbx::Vec3Attributes   Normals   = Geometry.getNormals();
            const ofbx::Vec2Attributes   UVs       = Geometry.getUVs();
            const ofbx::Vec4Attributes   Colors    = Geometry.getColors();

            // Bake FBX transform: WorldVertex = GlobalTransform * GeometricMatrix * LocalVertex.
            // Per-mesh GeometricMatrix is FBX-specific; ignoring it leaves meshes inconsistently oriented.
            const glm::mat4 GlobalMatrix    = ConvertMatrix(Mesh->getGlobalTransform());
            const glm::mat4 GeometricMatrix = ConvertMatrix(Mesh->getGeometricMatrix());
            const glm::mat4 MeshToWorld     = GlobalMatrix * GeometricMatrix;
            const glm::mat4 PosMatrix       = glm::scale(glm::mat4(1.0f), glm::vec3(SceneScale)) * MeshToWorld;
            const glm::mat3 NormalMatrix    = glm::transpose(glm::inverse(glm::mat3(MeshToWorld)));

            const int CPCount = Mesh->getGeometry()->getGeometryData().getPositions().count;

            struct FSkin
            {
                int32 Count = 0;
                TArray<int32, 4> Joints {};
                TArray<float, 4> Weights {};
            };

            TVector<FSkin> Skins(CPCount);
            if (bSkinned)
            {
                const int ClusterCount = OFBXSkin->getClusterCount();

                for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
                {
                    const ofbx::Cluster* Cluster = OFBXSkin->getCluster(ClusterIdx);

                    if (Cluster->getIndicesCount() == 0)
                    {
                        continue;
                    }

                    if (Cluster->getLink() == nullptr)
                    {
                        continue;
                    }

                    auto BoneIt = BoneNameToIndex.find(Cluster->getLink()->name);
                    if (BoneIt == BoneNameToIndex.end())
                    {
                        continue;
                    }
                    int32 BoneIndex = BoneIt->second;

                    const int*    Indices = Cluster->getIndices();
                    const double* Weights = Cluster->getWeights();
                    int           Count   = Cluster->getIndicesCount();

                    for (int i = 0; i < Count; ++i)
                    {
                        int CPIndex = Indices[i];
                        float Weight = static_cast<float>(Weights[i]);
                        FSkin& Skin = Skins[CPIndex];
                        if (Skin.Count < 4)
                        {
                            Skin.Weights[Skin.Count] = Weight;
                            Skin.Joints[Skin.Count] = BoneIndex;
                            ++Skin.Count;
                        }
                        else
                        {
                            int Min = 0;
                            for (int M = 1; M < 4; ++M)
                            {
                                if (Skin.Weights[M] < Skin.Weights[Min])
                                {
                                    Min = M;
                                }

                                if (Skin.Weights[Min] < Weight)
                                {
                                    Skin.Weights[Min] = Weight;
                                    Skin.Joints[Min]  = BoneIndex;
                                }
                            }
                        }
                    }
                }

                for (FSkin& Skin : Skins)
                {
                    float Sum = 0;
                    for (float Weight : Skin.Weights)
                    {
                        Sum += Weight;
                    }

                    if (Sum == 0.0f)
                    {
                        Skin.Weights[0] = 1.0f;
                        Skin.Weights[1] = Skin.Weights[2] = Skin.Weights[3] = 0.0f;
                        Skin.Joints[0] = Skin.Joints[1] = Skin.Joints[2] = Skin.Joints[3] = 0;
                    }
                    else
                    {
                        for (float& Weight : Skin.Weights)
                        {
                            Weight /= Sum;
                        }
                    }
                }
            }

            for (int PartitionIdx = 0; PartitionIdx < Geometry.getPartitionCount(); ++PartitionIdx)
            {
                uint32 StartIndex = (uint32)Result.Indices.size();

                const ofbx::GeometryPartition& Partition = Geometry.getPartition(PartitionIdx);

                for (int PolygonIdx = 0; PolygonIdx < Partition.polygon_count; ++PolygonIdx)
                {
                    const ofbx::GeometryPartition::Polygon& Polygon = Partition.polygons[PolygonIdx];

                    int TriangleIndices[128];
                    uint32 TriIndexCount = ofbx::triangulate(Geometry, Polygon, TriangleIndices);

                    for (uint32 i = 0; i < TriIndexCount; ++i)
                    {
                        int Index = TriangleIndices[i];

                        uint32 VertexIdx;
                        auto it = VertexIndexMap.find(Index);
                        if (it != VertexIndexMap.end())
                        {
                            VertexIdx = it->second;
                        }
                        else
                        {
                            ofbx::Vec3 Position = Positions.get(Index);
                            glm::vec3 PosLocal(Position.x, Position.y, Position.z);
                            glm::vec3 Pos = glm::vec3(PosMatrix * glm::vec4(PosLocal, 1.0f));

                            glm::vec3 Normal = glm::normalize(NormalMatrix * glm::vec3(0.0f, 1.0f, 0.0f));
                            if (Normals.values)
                            {
                                ofbx::Vec3 N = Normals.get(Index);
                                Normal = glm::normalize(NormalMatrix * glm::vec3(N.x, N.y, N.z));
                            }

                            glm::vec2 UV(0, 0);
                            if (UVs.values)
                            {
                                ofbx::Vec2 U = UVs.get(Index);

                                if (ImportOptions.bFlipUVs)
                                {
                                    U.y = 1.0f - U.y;
                                }

                                UV = glm::vec2(U.x, U.y);
                            }

                            glm::vec4 Col(1.0f);
                            if (Colors.values)
                            {
                                ofbx::Vec4 Color = Colors.get(Index);
                                Col = glm::vec4(Color.x, Color.y, Color.z, Color.w);
                            }

                            if (bSkinned)
                            {
                                FSkinnedVertex Vertex;
                                Vertex.Position = Pos;
                                Vertex.Normal   = PackNormal(Normal);
                                Vertex.Tangent  = 0;
                                Vertex.UV       = glm::packHalf2x16(UV);
                                Vertex.Color    = PackColor(Col);

                                glm::u8vec4 JointIndices{};
                                glm::vec4   JointWeights{};

                                if (Positions.indices)
                                {
                                    const FSkin& Skin = Skins[Positions.indices[Index]];
                                    for (int j = 0; j < Skin.Count; ++j)
                                    {
                                        uint8 BoneIndex = (uint8)Skin.Joints[j];
                                        float Weight    = Skin.Weights[j];

                                        JointIndices[j] = BoneIndex;
                                        JointWeights[j] = Weight;
                                    }
                                }

                                Vertex.JointIndices = JointIndices;
                                Vertex.JointWeights = glm::u8vec4(JointWeights * 255.0f);

                                VertexIdx = (uint32)Result.SkinnedVerts.size();
                                Result.SkinnedVerts.push_back(Vertex);
                            }
                            else
                            {
                                FVertex Vertex;
                                Vertex.Position = Pos;
                                Vertex.Normal   = PackNormal(Normal);
                                Vertex.Tangent  = 0;
                                Vertex.UV       = glm::packHalf2x16(UV);
                                Vertex.Color    = PackColor(Col);

                                VertexIdx = (uint32)Result.StaticVerts.size();
                                Result.StaticVerts.push_back(Vertex);
                            }

                            VertexIndexMap[Index] = VertexIdx;
                        }

                        Result.Indices.push_back(VertexIdx);
                    }
                }

                FGeometrySurface Surface;
                Surface.ID = Mesh->name;
                Surface.StartIndex = StartIndex;
                Surface.IndexCount = (uint32)Result.Indices.size() - StartIndex;
                Surface.MaterialIndex = (int16)PartitionIdx;
                Result.Surfaces.push_back(Surface);
            }
        });

        // Serial merge: indices and surface StartIndex rebased by running bases, source-order traversal stays deterministic.
        TUniquePtr<FMeshResource> StaticMesh = MakeUnique<FMeshResource>();
        StaticMesh->Vertices = TVector<FVertex>();
        StaticMesh->Name = FString(FileName) + "_Mesh";

        TUniquePtr<FMeshResource> SkinnedMesh = MakeUnique<FMeshResource>();
        SkinnedMesh->Vertices = TVector<FSkinnedVertex>();
        SkinnedMesh->bSkinnedMesh = true;
        SkinnedMesh->Name = FString(FileName) + "_SkeletalMesh";

        {
            size_t StaticVertCount = 0, StaticIdxCount = 0, StaticSurfCount = 0;
            size_t SkinnedVertCount = 0, SkinnedIdxCount = 0, SkinnedSurfCount = 0;
            for (const FFBXMeshResult& R : Results)
            {
                if (R.bSkinned)
                {
                    SkinnedVertCount += R.SkinnedVerts.size();
                    SkinnedIdxCount  += R.Indices.size();
                    SkinnedSurfCount += R.Surfaces.size();
                }
                else
                {
                    StaticVertCount += R.StaticVerts.size();
                    StaticIdxCount  += R.Indices.size();
                    StaticSurfCount += R.Surfaces.size();
                }
            }

            auto& StaticVertVec = eastl::get<TVector<FVertex>>(StaticMesh->Vertices);
            StaticVertVec.reserve(StaticVertCount);
            StaticMesh->Indices.reserve(StaticIdxCount);
            StaticMesh->GeometrySurfaces.reserve(StaticSurfCount);

            auto& SkinnedVertVec = eastl::get<TVector<FSkinnedVertex>>(SkinnedMesh->Vertices);
            SkinnedVertVec.reserve(SkinnedVertCount);
            SkinnedMesh->Indices.reserve(SkinnedIdxCount);
            SkinnedMesh->GeometrySurfaces.reserve(SkinnedSurfCount);
        }

        for (FFBXMeshResult& R : Results)
        {
            FMeshResource& Target = R.bSkinned ? *SkinnedMesh : *StaticMesh;

            const uint32 BaseVert = (uint32)Target.GetNumVertices();
            const uint32 BaseIdx  = (uint32)Target.Indices.size();

            if (R.bSkinned)
            {
                auto& Vec = eastl::get<TVector<FSkinnedVertex>>(Target.Vertices);
                Vec.insert(Vec.end(), R.SkinnedVerts.begin(), R.SkinnedVerts.end());
            }
            else
            {
                auto& Vec = eastl::get<TVector<FVertex>>(Target.Vertices);
                Vec.insert(Vec.end(), R.StaticVerts.begin(), R.StaticVerts.end());
            }

            for (uint32 LocalIdx : R.Indices)
            {
                Target.Indices.push_back(LocalIdx + BaseVert);
            }

            for (FGeometrySurface S : R.Surfaces)
            {
                S.StartIndex += BaseIdx;
                Target.GeometrySurfaces.push_back(S);
            }
        }

        TVector<FMeshResource*> ToFinalize;
        if (StaticMesh->GetNumVertices() > 0)
        {
            ToFinalize.push_back(StaticMesh.get());
        }
        if (SkinnedMesh->GetNumVertices() > 0)
        {
            ToFinalize.push_back(SkinnedMesh.get());
        }

        // When bSkipFinalization is set, the dialog runs heavy passes at commit; stats still built for preview.
        if (!ImportOptions.bSkipFinalization)
        {
            Task::ParallelFor((uint32)ToFinalize.size(), [&](uint32 i)
            {
                FMeshResource* Res = ToFinalize[i];
                if (ImportOptions.bOptimize)
                {
                    OptimizeNewlyImportedMesh(*Res);
                }
                GenerateMeshlets(*Res);
            });
        }

        for (FMeshResource* Res : ToFinalize)
        {
            AnalyzeMeshStatistics(*Res, ImportData.MeshStatistics);
        }

        if (StaticMesh->GetNumVertices() > 0)
        {
            ImportData.Resources.push_back(std::move(StaticMesh));
        }
        if (SkinnedMesh->GetNumVertices() > 0)
        {
            ImportData.Resources.push_back(std::move(SkinnedMesh));
        }

        return Move(ImportData);
    }
}
