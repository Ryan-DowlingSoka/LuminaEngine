#include "pch.h"
#include "CameraRigSystem.h"
#include "Core/Math/Math.h"
#include "Core/Math/Transform.h"
#include "Physics/Ray/RayCast.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/CameraFollowComponent.h"
#include "World/Entity/Components/SpringArmComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/Components/EntityTags.h"

namespace Lumina
{
    namespace Detail
    {
        // Frame-rate independent exponential smoothing weight. LagSpeed <= 0 snaps.
        static float SmoothAlpha(float LagSpeed, float Dt)
        {
            if (LagSpeed <= 0.0f)
            {
                return 1.0f;
            }
            return 1.0f - glm::exp(-LagSpeed * Dt);
        }

        // Rotation whose +Z (forward) axis points along Forward, +Y near WorldUp.
        // Matches the camera convention (forward = rotation * (0,0,1)).
        static glm::quat MakeLookRotation(const glm::vec3& Forward, const glm::vec3& WorldUp)
        {
            const glm::vec3 F = glm::normalize(Forward);
            glm::vec3 Up = WorldUp;
            if (glm::abs(glm::dot(F, Up)) > 0.999f)
            {
                Up = glm::vec3(0.0f, 0.0f, 1.0f);
                if (glm::abs(glm::dot(F, Up)) > 0.999f)
                {
                    Up = glm::vec3(1.0f, 0.0f, 0.0f);
                }
            }
            const glm::vec3 R = glm::normalize(glm::cross(Up, F));
            const glm::vec3 U = glm::cross(F, R);
            return glm::quat_cast(glm::mat3(R, U, F));
        }

        static void ApplyWorldPose(entt::registry& Registry, entt::entity Entity, const STransformComponent& Xform, const glm::vec3& Location, const glm::quat& Rotation)
        {
            FTransform World;
            World.Location = Location;
            World.Rotation = Rotation;
            World.Scale    = Xform.GetWorldScale();
            ECS::Utils::SetEntityWorldTransform(Registry, Entity, World);
        }
    }

    void SCameraRigSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = Context.GetRegistry();
        const float Dt = (float)Context.GetDeltaTime();

        //~ Follow: ease toward Target + Offset, optionally facing the target.
        auto FollowView = Registry.view<SCameraFollowComponent, STransformComponent>(entt::exclude<SDisabledTag, SSpringArmComponent>);
        for (entt::entity Entity : FollowView)
        {
            SCameraFollowComponent& Follow = FollowView.get<SCameraFollowComponent>(Entity);
            const entt::entity Target = (entt::entity)Follow.Target;
            if (!Registry.valid(Target) || !Registry.all_of<STransformComponent>(Target))
            {
                continue;
            }

            const STransformComponent& Xform       = FollowView.get<STransformComponent>(Entity);
            const STransformComponent& TargetXform = Registry.get<STransformComponent>(Target);

            const glm::vec3 TargetPos = TargetXform.GetWorldLocation();
            const glm::quat TargetRot = TargetXform.GetWorldRotation();

            const glm::vec3 DesiredPos = Follow.bWorldSpaceOffset
                ? TargetPos + Follow.Offset
                : TargetPos + TargetRot * Follow.Offset;

            glm::quat DesiredRot = Xform.GetWorldRotation();
            if (Follow.bLookAtTarget)
            {
                const glm::vec3 LookPoint = TargetPos + Follow.LookAtOffset;
                const glm::vec3 Dir = LookPoint - DesiredPos;
                if (glm::dot(Dir, Dir) > 1e-6f)
                {
                    DesiredRot = Detail::MakeLookRotation(Dir, glm::vec3(0.0f, 1.0f, 0.0f));
                }
            }

            if (!Follow.bInitialized)
            {
                Follow.CurrentPosition = DesiredPos;
                Follow.CurrentRotation = DesiredRot;
                Follow.bInitialized = true;
            }
            else
            {
                Follow.CurrentPosition = glm::mix(Follow.CurrentPosition, DesiredPos, Detail::SmoothAlpha(Follow.PositionLagSpeed, Dt));
                if (Follow.bLookAtTarget)
                {
                    glm::quat To = DesiredRot;
                    if (glm::dot(Follow.CurrentRotation, To) < 0.0f)
                    {
                        To = -To;
                    }
                    Follow.CurrentRotation = glm::slerp(Follow.CurrentRotation, To, Detail::SmoothAlpha(Follow.RotationLagSpeed, Dt));
                }
                else
                {
                    Follow.CurrentRotation = DesiredRot;
                }
            }

            Detail::ApplyWorldPose(Registry, Entity, Xform, Follow.CurrentPosition, Follow.CurrentRotation);
        }

        //~ Spring arm: place the camera a (collision-shortened) distance behind the pivot.
        auto ArmView = Registry.view<SSpringArmComponent, STransformComponent>(entt::exclude<SDisabledTag>);
        for (entt::entity Entity : ArmView)
        {
            SSpringArmComponent&        Arm   = ArmView.get<SSpringArmComponent>(Entity);
            const STransformComponent&  Xform = ArmView.get<STransformComponent>(Entity);

            const entt::entity Target = (entt::entity)Arm.Target;
            const bool bHasTarget = Registry.valid(Target) && Registry.all_of<STransformComponent>(Target);

            glm::vec3 PivotBase;
            glm::quat ControlRot;
            if (bHasTarget)
            {
                const STransformComponent& TargetXform = Registry.get<STransformComponent>(Target);
                PivotBase  = TargetXform.GetWorldLocation();
                ControlRot = Arm.bUseControlRotation ? Xform.GetWorldRotation() : TargetXform.GetWorldRotation();
            }
            else
            {
                PivotBase  = Xform.GetWorldLocation();
                ControlRot = Xform.GetWorldRotation();
            }

            const glm::vec3 Pivot = PivotBase + Arm.TargetOffset;

            if (!Arm.bInitialized)
            {
                Arm.CurrentPivot     = Pivot;
                Arm.CurrentArmLength = Arm.TargetArmLength;
                Arm.bInitialized = true;
            }
            else
            {
                Arm.CurrentPivot = glm::mix(Arm.CurrentPivot, Pivot, Detail::SmoothAlpha(Arm.PositionLagSpeed, Dt));
            }

            const glm::vec3 Back        = ControlRot * glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 SocketWorld = ControlRot * Arm.SocketOffset;
            const glm::vec3 DesiredEnd  = Arm.CurrentPivot + Back * Arm.TargetArmLength + SocketWorld;

            float DesiredLength = Arm.TargetArmLength;
            if (Arm.bDoCollisionTest && Arm.TargetArmLength > 0.0f)
            {
                SSphereCastSettings Settings;
                Settings.Start  = Arm.CurrentPivot + SocketWorld;
                Settings.End    = DesiredEnd;
                Settings.Radius = Arm.ProbeSize;
                Settings.IgnoreBodies.push_back(Context.GetEntityBodyID(Entity));
                if (bHasTarget)
                {
                    Settings.IgnoreBodies.push_back(Context.GetEntityBodyID(Target));
                }

                float Nearest = 1.0f;
                for (const SRayResult& Hit : Context.CastSphere(Settings))
                {
                    Nearest = glm::min(Nearest, Hit.Fraction);
                }
                DesiredLength = glm::max(0.0f, Arm.TargetArmLength * Nearest);
            }

            // Snap inward to avoid clipping; ease back out when the obstruction clears.
            if (DesiredLength < Arm.CurrentArmLength)
            {
                Arm.CurrentArmLength = DesiredLength;
            }
            else
            {
                Arm.CurrentArmLength = glm::mix(Arm.CurrentArmLength, DesiredLength, Detail::SmoothAlpha(Arm.LengthLagSpeed, Dt));
            }

            const glm::vec3 FinalPos = Arm.CurrentPivot + Back * Arm.CurrentArmLength + SocketWorld;
            Detail::ApplyWorldPose(Registry, Entity, Xform, FinalPos, ControlRot);
        }
    }
}
