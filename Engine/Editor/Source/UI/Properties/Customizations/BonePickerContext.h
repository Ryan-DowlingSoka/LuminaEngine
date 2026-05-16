#pragma once

namespace Lumina
{
    struct FSkeletonResource;

    // Editor-only context for FName properties tagged BonePicker. Asset editors
    // that author bone references (animation graph, etc.) push the skeleton they
    // are editing here before drawing the property panel; the FName customization
    // reads it back and renders a bone-tree popup instead of a plain text field.
    namespace BonePickerContext
    {
        // Pushes/pops the active skeleton on a small stack so re-entrant draws
        // (e.g. a nested property table) restore the outer skeleton correctly.
        void PushSkeleton(const FSkeletonResource* Skeleton);
        void PopSkeleton();

        const FSkeletonResource* GetActiveSkeleton();

        // RAII wrapper: pushes on construction, pops on destruction.
        struct FScope
        {
            explicit FScope(const FSkeletonResource* Skeleton) { PushSkeleton(Skeleton); }
            ~FScope()                                          { PopSkeleton(); }
            FScope(const FScope&) = delete;
            FScope& operator=(const FScope&) = delete;
        };
    }
}
