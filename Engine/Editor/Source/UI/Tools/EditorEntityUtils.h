#pragma once

#define USE_IMGUI_API
#include <imgui.h>
#include "ImGuizmo.h"
#include "Containers/String.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "World/Entity/Registry/EntityRegistry.h"
#include "glm/glm.hpp"

namespace Lumina
{
    class CWorld;
    struct SNameComponent;
    struct STransformComponent;
}

// Shared helpers used by FWorldEditorTool and FPrefabEditorTool — anything that
// touches the editor-only tag set, the transform/gizmo math, or the outliner
// row formatting belongs here so the two tools agree by construction.
namespace Lumina::EditorEntityUtils
{
    /** True for tags/components that are tool-internal bookkeeping and must NOT be
     *  duplicated when copying an entity, nor serialized into a prefab. */
    bool IsEditorOnlyComponent(const entt::type_info& Type);
    bool IsEditorOnlyComponent(entt::id_type TypeHash);

    /** Standard filter for CWorld::DuplicateEntity — drops the editor-only set above
     *  so duplicates don't carry the source's selection / clipboard / dirty flags. */
    bool DefaultDuplicateFilter(const entt::type_info& Type);

    /** Translate → Rotate → Scale → Translate. */
    void CycleGizmoOp(ImGuizmo::OPERATION& InOutOp);

    /** WORLD ↔ LOCAL. */
    void ToggleGizmoMode(ImGuizmo::MODE& InOutMode);

    /** Decompose a world-space matrix into the entity's local transform, accounting
     *  for any parent. Writes Local{Location,Rotation,Scale} via the transform setters
     *  so the dirty flag is raised. Safe to call when the entity has no parent. */
    void ApplyWorldMatrixToTransform(FEntityRegistry& Registry, entt::entity Entity, const glm::mat4& WorldMatrix);

    /** Standard formatting for an entity's outliner row: "<icon> <name> - (<id>)". */
    FFixedString MakeOutlinerDisplayName(const SNameComponent* Name, entt::entity Entity, const char* Icon = LE_ICON_CUBE);

    /** Camera-frame focus helper: returns world-space center + radius covering Entity
     *  and all descendants with mesh AABBs / transforms. Returns false if the entity
     *  has nothing focusable. */
    bool ComputeFocusBoundsForEntity(FEntityRegistry& Registry, entt::entity Entity, glm::vec3& OutCenter, float& OutRadius);
}
