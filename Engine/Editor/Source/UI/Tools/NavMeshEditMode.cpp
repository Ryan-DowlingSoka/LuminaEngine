#include "NavMeshEditMode.h"

#include "Tools/UI/ImGui/ImGuiX.h"
#include "World/Entity/Components/NavMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/World.h"

namespace Lumina
{
    namespace
    {
        entt::entity FindFirstNavMesh(CWorld* World)
        {
            if (!World) return entt::null;
            auto View = World->GetEntityRegistry().view<SNavMeshComponent>();
            for (auto E : View) return E;
            return entt::null;
        }

        const glm::vec4 kVolumeColor(0.95f, 0.65f, 0.10f, 1.0f);
        const glm::vec4 kBakedColor (0.30f, 0.85f, 0.45f, 1.0f);
        const glm::vec4 kBakingColor(0.85f, 0.85f, 0.20f, 1.0f);
    }

    entt::entity FNavMeshEditMode::CreateNavMeshBounds(CWorld* World, const glm::vec3& WorldLocation)
    {
        if (!World) return entt::null;

        FTransform Xform;
        Xform.Location = WorldLocation;
        entt::entity Entity = World->ConstructEntity("NavMeshBounds", Xform);

        SNavMeshComponent& Nav = World->GetEntityRegistry().emplace<SNavMeshComponent>(Entity);
        Nav.Center  = WorldLocation;
        Nav.Extents = glm::vec3(64.0f, 16.0f, 64.0f);
        // Don't auto-bake on create. The user usually wants to position +
        // resize the bounds first; an explicit Bake click matches Unreal.
        return Entity;
    }

    void FNavMeshEditMode::DrawToolbar(CWorld* World, float ButtonSize)
    {
        if (!World)
        {
            return;
        }

        const entt::entity NavEntity = FindFirstNavMesh(World);
        if (NavEntity == entt::null)
        {
            return;
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        SNavMeshComponent& Nav = World->GetEntityRegistry().get<SNavMeshComponent>(NavEntity);

        const bool bBaking = Nav.Runtime.State == ENavBakeState::Building;
        ImGui::PushStyleColor(ImGuiCol_Button, bBaking
            ? ImVec4(kBakingColor.x, kBakingColor.y, kBakingColor.z, kBakingColor.w)
            : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(bBaking ? "Baking..." : "Bake Navigation", ImVec2(0, ButtonSize)) && !bBaking)
        {
            Nav.bBakeRequested = true;
        }
        ImGui::PopStyleColor();
    }

    void FNavMeshEditMode::DrawOverlay(CWorld* World) const
    {
        if (!World)
        {
            return;
        }

        auto& Reg = World->GetEntityRegistry();
        auto View = Reg.view<SNavMeshComponent>();
        for (entt::entity Entity : View)
        {
            SNavMeshComponent& Nav = View.get<SNavMeshComponent>(Entity);

            glm::vec3 Center = Nav.Center;
            if (auto* Xform = Reg.try_get<STransformComponent>(Entity))
            {
                // Mirror the transform's location so the user can drag the
                // entity in the viewport and the bounds tracks. The
                // serialized Center stays write-through for the next bake.
                Center = Xform->GetWorldTransform().Location;
                Nav.Center = Center;
            }

            const glm::vec4 Color = Nav.HasBakedData() ? kBakedColor : kVolumeColor;
            // Duration=-1 marks the box "single frame"; the editor overlay
            // re-emits each tick so the bounds wireframe is always present.
            World->DrawBox(Center, Nav.Extents, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), Color, 1.5f, true, -1.0f);
        }
    }
}
