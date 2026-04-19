#pragma once

#include "Core/Math/AABB.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Memory/SmartPtr.h"
#include "Animation.generated.h"

namespace Lumina
{
    class CSkeleton;
    struct FSkeletonResource;

    struct FAnimationChannel
    {
        enum class ETargetPath : uint8
        {
            Translation,
            Rotation,
            Scale,
            Weights
        };
    
        FName TargetBone; 
        ETargetPath TargetPath;
        TVector<float> Timestamps;
        TVector<glm::vec3> Translations;
        TVector<glm::quat> Rotations;
        TVector<glm::vec3> Scales;
        
        friend FArchive& operator << (FArchive& Ar, FAnimationChannel& Data)
        {
            Ar << Data.TargetBone;
            Ar << Data.TargetPath;
            Ar << Data.Timestamps;
            Ar << Data.Translations;
            Ar << Data.Rotations;
            Ar << Data.Scales;
            
            return Ar;
        }
    };
    
    struct FAnimationNotify
    {
        FName NotifyName;
        float Time;
        FName NotifyTrack;
        glm::vec4 Color;
    
        friend FArchive& operator << (FArchive& Ar, FAnimationNotify& Data)
        {
            Ar << Data.NotifyName;
            Ar << Data.Time;
            Ar << Data.NotifyTrack;
            Ar << Data.Color;
            return Ar;
        }
    };
    
    struct FAnimationNotifyState
    {
        FName NotifyName;
        float StartTime;
        float EndTime;
        FName NotifyTrack;
        glm::vec4 Color;
    
        friend FArchive& operator << (FArchive& Ar, FAnimationNotifyState& Data)
        {
            Ar << Data.NotifyName;
            Ar << Data.StartTime;
            Ar << Data.EndTime;
            Ar << Data.NotifyTrack;
            Ar << Data.Color;
            return Ar;
        }
    };
        
    struct FAnimationResource
    {
        FName Name;
        float Duration;
        TVector<FAnimationChannel> Channels;
        TVector<FAnimationNotify> Notifies;
        TVector<FAnimationNotifyState> NotifyStates;
        
        
        friend FArchive& operator << (FArchive& Ar, FAnimationResource& Data)
        {
            Ar << Data.Name;
            Ar << Data.Duration;
            Ar << Data.Channels;
            Ar << Data.Notifies;
            Ar << Data.NotifyStates;
            
            return Ar;
        }
    };
    
    
    REFLECT()
    class RUNTIME_API CAnimation : public CObject
    {
        GENERATED_BODY()
        
        friend class CMeshFactory;
        
    public:
        
        void Serialize(FArchive& Ar) override;
        
        bool IsAsset() const override { return true; }
        
        void SamplePose(float Time, FSkeletonResource* SkeletonResource, TArray<glm::mat4, 255>& OutBoneTransforms);
        
        float GetDuration() const { return AnimationResource->Duration; }
        FAnimationResource* GetAnimationResource() const { return AnimationResource.get(); }
        
        /** The skeleton asset this animation targets. */
        PROPERTY(Editable, Category = "Skeleton")
        TObjectPtr<CSkeleton> Skeleton;
        
    private:
        
        TUniquePtr<FAnimationResource> AnimationResource;
    };
}
