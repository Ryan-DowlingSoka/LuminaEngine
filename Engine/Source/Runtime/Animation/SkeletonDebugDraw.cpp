#include "pch.h"
#include "SkeletonDebugDraw.h"

#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Renderer/MeshData.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "World/World.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"

namespace Lumina::SkeletonDebugDraw
{
    void ComputeGlobalBoneTransforms(const FSkeletonResource* Skeleton,
                                     const TVector<glm::mat4>& BoneTransforms,
                                     TVector<glm::mat4>& OutGlobals)
    {
        const int32 NumBones = Skeleton ? Skeleton->GetNumBones() : 0;
        OutGlobals.resize(NumBones);
        if (NumBones == 0)
        {
            return;
        }

        // Live pose has Global*InvBind; recover model-space by undoing InvBind.
        if ((int32)BoneTransforms.size() == NumBones)
        {
            for (int32 i = 0; i < NumBones; ++i)
            {
                OutGlobals[i] = BoneTransforms[i] * glm::inverse(Skeleton->GetBone(i).InvBindMatrix);
            }
        }
        else
        {
            for (int32 i = 0; i < NumBones; ++i)
            {
                const FSkeletonResource::FBoneInfo& Bone = Skeleton->GetBone(i);
                OutGlobals[i] = (Bone.ParentIndex >= 0)
                    ? OutGlobals[Bone.ParentIndex] * Bone.LocalTransform
                    : Bone.LocalTransform;
            }
        }
    }

    namespace
    {
        void DrawOctahedralBone(IPrimitiveDrawInterface* DI, const glm::vec3& From, const glm::vec3& To,
                                const glm::vec4& Color, float Thickness, bool bDepthTest)
        {
            const glm::vec3 Axis = To - From;
            const float Length   = glm::length(Axis);
            if (Length < 1e-5f)
            {
                return;
            }

            const glm::vec3 Dir  = Axis / Length;
            glm::vec3 Up         = (fabsf(Dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 Side = glm::normalize(glm::cross(Dir, Up));
            Up                   = glm::normalize(glm::cross(Side, Dir));

            const float RidgeDist   = Length * 0.18f;
            const float RidgeRadius = glm::clamp(Length * 0.10f, 0.006f, 0.05f);
            const glm::vec3 Center  = From + Dir * RidgeDist;

            const glm::vec3 R[4] =
            {
                Center + Side * RidgeRadius,
                Center + Up   * RidgeRadius,
                Center - Side * RidgeRadius,
                Center - Up   * RidgeRadius,
            };

            for (int k = 0; k < 4; ++k)
            {
                DI->DrawLine(From, R[k], Color, Thickness, bDepthTest);          // base fan
                DI->DrawLine(R[k], To,   Color, Thickness, bDepthTest);          // tip fan
                DI->DrawLine(R[k], R[(k + 1) & 3], Color, Thickness, bDepthTest); // ridge ring
            }
        }
    }

    void DrawSkeleton(IPrimitiveDrawInterface* DrawInterface,
                      const FSkeletonResource* Skeleton,
                      const TVector<glm::mat4>& GlobalBoneTransforms,
                      const glm::mat4& MeshWorldMatrix,
                      const FOptions& Options)
    {
        if (DrawInterface == nullptr || Skeleton == nullptr)
        {
            return;
        }

        const int32 NumBones = Skeleton->GetNumBones();
        if ((int32)GlobalBoneTransforms.size() != NumBones)
        {
            return;
        }

        for (int32 i = 0; i < NumBones; ++i)
        {
            const FSkeletonResource::FBoneInfo& Bone = Skeleton->GetBone(i);
            const glm::mat4 WorldBone = MeshWorldMatrix * GlobalBoneTransforms[i];
            const glm::vec3 Position  = glm::vec3(WorldBone[3]);

            if (Options.bBones && Bone.ParentIndex >= 0)
            {
                const glm::vec3 ParentPos = glm::vec3((MeshWorldMatrix * GlobalBoneTransforms[Bone.ParentIndex])[3]);
                if (Options.bOctahedral)
                {
                    DrawOctahedralBone(DrawInterface, ParentPos, Position, Options.BoneColor, Options.BoneThickness, Options.bDepthTest);
                }
                else
                {
                    DrawInterface->DrawLine(ParentPos, Position, Options.BoneColor, Options.BoneThickness, Options.bDepthTest);
                }
            }

            if (Options.bJoints)
            {
                const bool  bRoot  = (Bone.ParentIndex < 0);
                const glm::vec4 Color = bRoot ? Options.RootColor : Options.JointColor;
                const float Radius = bRoot ? Options.JointRadius * 1.8f : Options.JointRadius;
                DrawInterface->DrawSphere(Position, Radius, Color, 8, Options.BoneThickness, Options.bDepthTest);
            }

            if (Options.bAxes)
            {
                const glm::vec3 AxisX = glm::normalize(glm::vec3(WorldBone[0])) * Options.AxisLength;
                const glm::vec3 AxisY = glm::normalize(glm::vec3(WorldBone[1])) * Options.AxisLength;
                const glm::vec3 AxisZ = glm::normalize(glm::vec3(WorldBone[2])) * Options.AxisLength;
                DrawInterface->DrawLine(Position, Position + AxisX, glm::vec4(1.0f, 0.25f, 0.25f, 1.0f), 1.5f, Options.bDepthTest);
                DrawInterface->DrawLine(Position, Position + AxisY, glm::vec4(0.35f, 1.0f, 0.35f, 1.0f), 1.5f, Options.bDepthTest);
                DrawInterface->DrawLine(Position, Position + AxisZ, glm::vec4(0.35f, 0.55f, 1.0f, 1.0f), 1.5f, Options.bDepthTest);
            }
        }
    }

    namespace
    {
        // Shared iteration: invoke Callback(skeleton, meshWorld, globals) for every drawable
        // skeletal mesh in the world. Keeps DrawWorldSkeletons / GatherWorldBoneLabels in sync.
        template <typename TFunc>
        void ForEachSkeletalMesh(CWorld* World, TFunc&& Callback)
        {
            if (World == nullptr)
            {
                return;
            }

            FEntityRegistry& Registry = World->GetEntityRegistry();
            auto View = Registry.view<SSkeletalMeshComponent, STransformComponent>();

            TVector<glm::mat4> Globals;
            for (entt::entity Entity : View)
            {
                const SSkeletalMeshComponent& Mesh = View.get<SSkeletalMeshComponent>(Entity);
                if (!Mesh.SkeletalMesh.IsValid())
                {
                    continue;
                }

                CSkeletalMesh* SkeletalMesh = Mesh.SkeletalMesh;
                if (!SkeletalMesh->Skeleton.IsValid())
                {
                    continue;
                }

                FSkeletonResource* Skeleton = SkeletalMesh->Skeleton->GetSkeletonResource();
                if (Skeleton == nullptr || Skeleton->GetNumBones() == 0)
                {
                    continue;
                }

                const glm::mat4 MeshWorld = View.get<STransformComponent>(Entity).GetWorldMatrix();
                ComputeGlobalBoneTransforms(Skeleton, Mesh.BoneTransforms, Globals);
                Callback(Skeleton, MeshWorld, Globals);
            }
        }
    }

    void DrawWorldSkeletons(CWorld* World, IPrimitiveDrawInterface* DrawInterface, const FOptions& Options)
    {
        if (DrawInterface == nullptr)
        {
            return;
        }

        ForEachSkeletalMesh(World, [&](const FSkeletonResource* Skeleton, const glm::mat4& MeshWorld, const TVector<glm::mat4>& Globals)
        {
            DrawSkeleton(DrawInterface, Skeleton, Globals, MeshWorld, Options);
        });
    }

    void GatherWorldBoneLabels(CWorld* World, TVector<FBoneLabel>& OutLabels)
    {
        ForEachSkeletalMesh(World, [&](const FSkeletonResource* Skeleton, const glm::mat4& MeshWorld, const TVector<glm::mat4>& Globals)
        {
            const int32 NumBones = Skeleton->GetNumBones();
            for (int32 i = 0; i < NumBones; ++i)
            {
                OutLabels.push_back({ Skeleton->GetBone(i).Name, glm::vec3((MeshWorld * Globals[i])[3]) });
            }
        });
    }
}
