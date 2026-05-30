#pragma once

#include "Core/Math/AABB.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Versioning/CoreVersion.h"
#include "Memory/SmartPtr.h"
#include "Animation.generated.h"

namespace Lumina
{
    class CSkeleton;
    struct FSkeletonResource;
    struct FPose;

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
        TVector<FVector3> Translations;
        TVector<FQuat> Rotations;
        TVector<FVector3> Scales;
        
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
        FVector4 Color;
    
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
        FVector4 Color;
    
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

        // Notify lanes in display order; persisted separately so empty tracks and ordering survive
        // save/reload. A notify references its lane by name (NotifyTrack).
        TVector<FName> NotifyTracks;


        friend FArchive& operator << (FArchive& Ar, FAnimationResource& Data)
        {
            Ar << Data.Name;
            Ar << Data.Duration;
            Ar << Data.Channels;
            Ar << Data.Notifies;
            Ar << Data.NotifyStates;
            Ar << Data.NotifyTracks;

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
        
        /** Writes (Global * InvBind) per bone; bones without channels keep their bind-pose local transform. */
        void SamplePose(float Time, FSkeletonResource* RESTRICT InSkeleton, TVector<FMatrix4>& RESTRICT OutBoneTransforms) const;

        /** Samples the clip into a local-space TRS pose; bones without channels keep their bind-pose local transform. */
        void SampleLocalPose(float Time, FSkeletonResource* RESTRICT InSkeleton, FPose& RESTRICT OutPose) const;

        float GetDuration() const { return AnimationResource->Duration; }
        FAnimationResource* GetAnimationResource() const { return AnimationResource.get(); }

        const TVector<FAnimationNotify>& GetNotifies() const { return AnimationResource->Notifies; }
        const TVector<FAnimationNotifyState>& GetNotifyStates() const { return AnimationResource->NotifyStates; }
        bool HasNotifies() const { return !AnimationResource->Notifies.empty() || !AnimationResource->NotifyStates.empty(); }

        PROPERTY(Editable, Category = "Skeleton")
        TObjectPtr<CSkeleton> Skeleton;
        
    private:
        
        TUniquePtr<FAnimationResource> AnimationResource;
    };
}
