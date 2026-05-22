#include "EditorEntityUtils.h"

#include "Components/EditorEntityTags.h"
#include "Containers/String.h"
#include "EASTL/string.h"
#include "Core/Math/AABB.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/World.h"
#include "glm/gtx/matrix_decompose.hpp"

namespace Lumina::EditorEntityUtils
{
    bool IsEditorOnlyComponent(const entt::type_info& Type)
    {
        return IsEditorOnlyComponent(Type.hash());
    }

    bool IsEditorOnlyComponent(entt::id_type TypeHash)
    {
        // Mirror this list in CommitPreviewWorldToPrefab() / DuplicateEntity filters so
        // every editor-only state is excluded from save and copy by one source of truth.
        return TypeHash == entt::type_hash<FRelationshipComponent>::value()
            || TypeHash == entt::type_hash<FSelectedInEditorComponent>::value()
            || TypeHash == entt::type_hash<FHideInSceneOutliner>::value()
            || TypeHash == entt::type_hash<FEditorComponent>::value()
            || TypeHash == entt::type_hash<FLastSelectedTag>::value()
            || TypeHash == entt::type_hash<FCopiedTag>::value()
            || TypeHash == entt::type_hash<FNeedsTransformUpdate>::value();
    }

    bool DefaultDuplicateFilter(const entt::type_info& Type)
    {
        // Same set, minus FRelationshipComponent and FNeedsTransformUpdate which CWorld::DuplicateEntity
        // handles itself (it rebuilds parent/child links and re-emits the dirty flag).
        const entt::id_type Hash = Type.hash();
        return !(Hash == entt::type_hash<FSelectedInEditorComponent>::value()
              || Hash == entt::type_hash<FCopiedTag>::value()
              || Hash == entt::type_hash<FLastSelectedTag>::value());
    }

    void CycleGizmoOp(ImGuizmo::OPERATION& InOutOp)
    {
        switch (InOutOp)
        {
        case ImGuizmo::TRANSLATE: InOutOp = ImGuizmo::ROTATE;    break;
        case ImGuizmo::ROTATE:    InOutOp = ImGuizmo::SCALE;     break;
        case ImGuizmo::SCALE:     InOutOp = ImGuizmo::TRANSLATE; break;
        default:                  InOutOp = ImGuizmo::TRANSLATE; break;
        }
    }

    void ToggleGizmoMode(ImGuizmo::MODE& InOutMode)
    {
        InOutMode = (InOutMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }

    void ApplyWorldMatrixToTransform(FEntityRegistry& Registry, entt::entity Entity, const glm::mat4& WorldMatrix)
    {
        if (!Registry.valid(Entity))
        {
            return;
        }

        STransformComponent* Transform = Registry.try_get<STransformComponent>(Entity);
        if (Transform == nullptr)
        {
            return;
        }

        glm::mat4 LocalMatrix = WorldMatrix;
        if (FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Entity))
        {
            if (Rel->Parent != entt::null && Registry.valid(Rel->Parent))
            {
                if (STransformComponent* ParentTransform = Registry.try_get<STransformComponent>(Rel->Parent))
                {
                    LocalMatrix = glm::inverse(ParentTransform->GetWorldMatrix()) * WorldMatrix;
                }
            }
        }

        glm::vec3 LocalLocation, LocalScale, LocalSkew;
        glm::quat LocalRotation;
        glm::vec4 LocalPersp;
        glm::decompose(LocalMatrix, LocalScale, LocalRotation, LocalLocation, LocalSkew, LocalPersp);

        Transform->SetLocalLocation(LocalLocation);
        Transform->SetLocalRotation(LocalRotation);
        Transform->SetLocalScale(LocalScale);
    }

    FFixedString MakeOutlinerDisplayName(const SNameComponent* Name, entt::entity Entity, const char* Icon)
    {
        FFixedString Out;
        Out.append(Icon).append(" ");
        Out.append(Name ? Name->Name.c_str() : "<unnamed>");
        Out.append_convert(FString(" - (" + eastl::to_string(entt::to_integral(Entity)) + ")"));
        return Out;
    }

    bool ComputeFocusBoundsForEntity(FEntityRegistry& Registry, entt::entity Entity, glm::vec3& OutCenter, float& OutRadius)
    {
        if (!Registry.valid(Entity))
        {
            return false;
        }

        glm::vec3 Min(FLT_MAX);
        glm::vec3 Max(-FLT_MAX);
        bool bAny = false;

        auto Accumulate = [&](entt::entity E)
        {
            if (!Registry.valid(E))
            {
                return;
            }

            const STransformComponent* Transform = Registry.try_get<STransformComponent>(E);
            if (Transform == nullptr)
            {
                return;
            }

            const glm::mat4 WorldMatrix = Transform->GetWorldMatrix();

            if (const SStaticMeshComponent* Mesh = Registry.try_get<SStaticMeshComponent>(E))
            {
                if (Mesh->StaticMesh)
                {
                    const FAABB Box = Mesh->GetAABB().ToWorld(WorldMatrix);
                    Min = glm::min(Min, Box.Min);
                    Max = glm::max(Max, Box.Max);
                    bAny = true;
                    return;
                }
            }

            if (const SSkeletalMeshComponent* Skinned = Registry.try_get<SSkeletalMeshComponent>(E))
            {
                if (Skinned->SkeletalMesh)
                {
                    const FAABB Box = Skinned->GetAABB().ToWorld(WorldMatrix);
                    Min = glm::min(Min, Box.Min);
                    Max = glm::max(Max, Box.Max);
                    bAny = true;
                    return;
                }
            }

            const glm::vec3 Loc = Transform->GetWorldLocation();
            Min = glm::min(Min, Loc);
            Max = glm::max(Max, Loc);
            bAny = true;
        };

        Accumulate(Entity);
        ECS::Utils::ForEachDescendant(Registry, Entity, [&](entt::entity Desc)
        {
            Accumulate(Desc);
        });

        if (!bAny)
        {
            return false;
        }

        OutCenter = (Min + Max) * 0.5f;
        OutRadius = glm::max(glm::length(Max - Min) * 0.5f, 0.5f);
        return true;
    }

    bool GetEntityDrawBox(FEntityRegistry& Registry, entt::entity Entity, glm::vec3& OutCenter, glm::vec3& OutHalfExtents, glm::quat& OutRotation)
    {
        if (!Registry.valid(Entity))
        {
            return false;
        }

        const STransformComponent* Transform = Registry.try_get<STransformComponent>(Entity);
        if (Transform == nullptr)
        {
            return false;
        }

        OutRotation = Transform->GetWorldRotation();

        // Padding so the box sits just outside the mesh silhouette.
        const glm::vec3 WorldScale = Transform->GetWorldScale();

        // Resolve the local-space AABB if the entity has a renderable mesh asset.
        const SStaticMeshComponent*   Mesh    = Registry.try_get<SStaticMeshComponent>(Entity);
        const SSkeletalMeshComponent* Skinned = Registry.try_get<SSkeletalMeshComponent>(Entity);

        FAABB Local;
        bool  bHasBounds = false;
        if (Mesh && Mesh->StaticMesh)
        {
            Local = Mesh->GetAABB();
            bHasBounds = true;
        }
        else if (Skinned && Skinned->SkeletalMesh)
        {
            Local = Skinned->GetAABB();
            bHasBounds = true;
        }

        if (bHasBounds)
        {
            constexpr float Padding = 1.05f;
            const glm::vec3 LocalCenter = (Local.Min + Local.Max) * 0.5f;
            const glm::vec3 LocalHalf   = (Local.Max - Local.Min) * 0.5f;

            OutCenter      = Transform->GetWorldLocation() + (OutRotation * (LocalCenter * WorldScale));
            OutHalfExtents = LocalHalf * WorldScale * Padding;
        }
        else
        {
            // No mesh bounds (lights, audio, empties, ...): a unit box scaled by the transform.
            OutCenter      = Transform->GetWorldLocation();
            OutHalfExtents = WorldScale;
        }

        return true;
    }

    void DrawEntityBounds(CWorld* World, entt::entity Entity, const glm::vec4& Color, float Thickness, bool bDepthTest)
    {
        glm::vec3 Center, HalfExtents;
        glm::quat Rotation;
        if (World && GetEntityDrawBox(World->GetEntityRegistry(), Entity, Center, HalfExtents, Rotation))
        {
            World->DrawBoxCorners(Center, HalfExtents, Rotation, Color, Thickness, bDepthTest);
        }
    }

    void DrawEntitySelectionBox(CWorld* World, entt::entity Entity, const glm::vec4& Color, float CornerFraction, float Thickness, bool bDepthTest)
    {
        glm::vec3 Center, HalfExtents;
        glm::quat Rotation;
        if (World && GetEntityDrawBox(World->GetEntityRegistry(), Entity, Center, HalfExtents, Rotation))
        {
            World->DrawBoxCorners(Center, HalfExtents, Rotation, Color, CornerFraction, Thickness, bDepthTest);
        }
    }
}
