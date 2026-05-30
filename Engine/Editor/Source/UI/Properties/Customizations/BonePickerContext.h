#pragma once

namespace Lumina
{
    struct FSkeletonResource;

    // Context for FName properties tagged BonePicker: asset editors push the edited
    // skeleton here so the FName customization renders a bone-tree popup.
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
