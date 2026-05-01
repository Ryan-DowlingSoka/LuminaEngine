#pragma once
#include "Containers/Array.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "PostProcessSettings.h"
#include "PostProcessComponent.generated.h"

namespace Lumina
{
    class CMaterialInterface;
    /**
     * Volumetric post-process override. Add to any entity with a
     * STransformComponent. The volume's box (BoxExtent in local space,
     * transformed by the entity) is tested against the active camera each
     * frame; while the camera is inside, the volume's Settings are blended
     * onto whatever the camera's own SCameraComponent::PostProcess provides.
     *
     * Set bInfiniteExtent for a global override that's always active --
     * useful for the level-default look without wrapping the playable area
     * in a giant box.
     *
     * Multiple volumes stack: lowest Priority blends first, highest blends
     * last (i.e. wins ties). Use BlendDistance to fade the contribution at
     * the box boundary so designers don't get a hard pop on entry.
     */
    REFLECT(Component)
    struct RUNTIME_API SPostProcessComponent
    {
        GENERATED_BODY()

        /** Master enable. Disabled volumes contribute nothing. */
        PROPERTY(Editable, Category = "Volume")
        bool bEnabled = true;

        /** When true the BoxExtent is ignored and this volume is always
         *  active. Use one of these as the level's "global look". */
        PROPERTY(Editable, Category = "Volume")
        bool bInfiniteExtent = false;

        /** Half-extent of the box in local space (i.e. the box is 2 *
         *  BoxExtent on each axis, centered on the entity). The world-
         *  space box rotates and scales with the transform. */
        PROPERTY(Editable, Category = "Volume", ClampMin = 0.01f)
        glm::vec3 BoxExtent = glm::vec3(500.0f);

        /** World-space distance over which the volume fades out at its
         *  boundary. 0 == hard edge (instant pop on enter/exit). 100..500
         *  is typical for outdoor mood transitions. */
        PROPERTY(Editable, Category = "Volume", ClampMin = 0.0f, ClampMax = 5000.0f)
        float BlendDistance = 100.0f;

        /** How strongly this volume's settings replace the camera/lower
         *  priority volumes when fully inside. 1.0 == full override,
         *  0.5 == 50% mix with what was there, 0.0 == do nothing. */
        PROPERTY(Editable, Category = "Volume", ClampMin = 0.0f, ClampMax = 1.0f, Delta = 0.01f)
        float BlendWeight = 1.0f;

        /** Higher priority volumes blend last and so override lower ones.
         *  Ties resolve by entity iteration order (effectively unspecified
         *  -- give your volumes distinct priorities if it matters). */
        PROPERTY(Editable, Category = "Volume")
        int32 Priority = 0;

        /** The grading + tone-mapping settings this volume contributes.
         *  Identical layout to the camera's own PostProcess. */
        PROPERTY(Editable, Category = "Settings")
        SPostProcessSettings Settings;

        /** Post-process materials this volume contributes. While the camera
         *  is inside the volume (or always, if bInfiniteExtent) these
         *  materials are appended to the active list and applied after the
         *  camera's own materials. Order within the list is preserved.
         *  Materials must have MaterialType = PostProcess. */
        PROPERTY(Editable, Category = "Settings")
        TVector<TObjectPtr<CMaterialInterface>> PostProcessMaterials;
    };
}
