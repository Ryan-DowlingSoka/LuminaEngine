#include "pch.h"
#include "Animation.h"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/string_cast.hpp>

#include "Renderer/MeshData.h"


namespace Lumina
{
    namespace Detail
    {
            static glm::vec3 SampleVec3(const TVector<float>& Times, const TVector<glm::vec3>& Values, float Time)
    {
        if (Times.empty() || Values.empty())
        {
            return glm::vec3(0.0f);
        }
        
        if (Times.size() == 1)
        {
            return Values[0];
        }
        
        if (Time <= Times[0])
        {
            return Values[0];
        }
        
        if (Time >= Times[Times.size() - 1])
        {
            return Values[Values.size() - 1];
        }
        
        for (size_t i = 0; i < Times.size() - 1; ++i)
        {
            if (Time >= Times[i] && Time < Times[i + 1])
            {
                float DeltaTime = Times[i + 1] - Times[i];
                float BlendFactor = (Time - Times[i]) / DeltaTime;
                
                return glm::mix(Values[i], Values[i + 1], BlendFactor);
            }
        }
        
        return Values[Values.size() - 1];
    }

    static glm::quat SampleQuat(const TVector<float>& Times, const TVector<glm::quat>& Values, float Time)
    {
        if (Times.empty() || Values.empty())
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
    
        if (Times.size() == 1)
        {
            return Values[0];
        }
    
        if (Time <= Times[0])
        {
            return Values[0];
        }
    
        if (Time >= Times[Times.size() - 1])
        {
            return Values[Values.size() - 1];
        }
    
        for (size_t i = 0; i < Times.size() - 1; ++i)
        {
            if (Time >= Times[i] && Time < Times[i + 1])
            {
                float DeltaTime = Times[i + 1] - Times[i];
                float BlendFactor = (Time - Times[i]) / DeltaTime;
            
                glm::quat Q0 = Values[i];
                glm::quat Q1 = Values[i + 1];
                
                if (glm::dot(Q0, Q1) < 0.0f)
                {
                    Q1 = -Q1;
                }
            
                return glm::slerp(Q0, Q1, BlendFactor);
            }
        }
    
        return Values[Values.size() - 1];
    }
    }
    
    
    void CAnimation::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
        
        if (!AnimationResource)
        {
            AnimationResource = MakeUnique<FAnimationResource>();
        }
        
        Ar << *AnimationResource;
    }
    
    void CAnimation::SamplePose(float Time, FSkeletonResource* RESTRICT SkeletonResource, TArray<glm::mat4, 255>& RESTRICT OutBoneTransforms)
    {
        LUMINA_PROFILE_SCOPE();
        
        const int32 NumBones = SkeletonResource->GetNumBones();
    
        TVector<glm::mat4> Local(NumBones);
        for (int i = 0; i < NumBones; ++i)
        {
            const auto& Bone = SkeletonResource->GetBone(i);
            Local[i] = Bone.LocalTransform;
        }
    
        for (const FAnimationChannel& Channel : AnimationResource->Channels)
        {
            int i = SkeletonResource->FindBoneIndex(Channel.TargetBone);
            if (i < 0 || i >= NumBones)
            {
                continue;
            }
            
            glm::vec3 Translation;
            glm::quat Rotation;
            glm::vec3 Scale;
            glm::vec3 Skew;
            glm::vec4 Perspective;
            glm::decompose(Local[i], Scale, Rotation, Translation, Skew, Perspective);

            switch (Channel.TargetPath)
            {
            case FAnimationChannel::ETargetPath::Translation:
                Translation = Detail::SampleVec3(Channel.Timestamps, Channel.Translations, Time);
                break;
    
            case FAnimationChannel::ETargetPath::Rotation:
                Rotation = Detail::SampleQuat(Channel.Timestamps, Channel.Rotations, Time);
                break;
    
            case FAnimationChannel::ETargetPath::Scale:
                Scale = Detail::SampleVec3(Channel.Timestamps, Channel.Scales, Time);
                break;
                
            case FAnimationChannel::ETargetPath::Weights:
                break;
                
            default:
                break;
            }
            
            Local[i] =
                glm::translate(glm::mat4(1.0f), Translation) *
                glm::mat4_cast(Rotation) *
                glm::scale(glm::mat4(1.0f), Scale);
        }
        
        TVector<glm::mat4> Global(NumBones);
        for (int i = 0; i < NumBones; ++i)
        {
            const auto& Bone = SkeletonResource->GetBone(i);
            if (Bone.ParentIndex == INDEX_NONE)
            {
                Global[i] = Local[i];
            }
            else
            {
                Global[i] = Global[Bone.ParentIndex] * Local[i];
            }
        }
    
        for (int i = 0; i < NumBones; ++i)
        {
            const auto& Bone = SkeletonResource->GetBone(i);
            OutBoneTransforms[i] = Global[i] * Bone.InvBindMatrix;
        }
    }
}
