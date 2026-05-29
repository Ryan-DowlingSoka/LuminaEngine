#pragma once

#include "Containers/Function.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectHandleTyped.h"


namespace Lumina
{
    class CObject;
    class FArchive;

    /** Stable, gameplay-facing identity for an asset that's been marked
     *  EAssetFlags::Primary in FAssetData. Uses the asset's FAssetName as
     *  the primary key, so e.g. /Game/Characters/Hero.lasset is reachable
     *  as FPrimaryAssetId(FName("Hero")). One Name → at most one Primary
     *  asset; multiple Primaries sharing a Name is a registry error. */
    struct RUNTIME_API FPrimaryAssetId
    {
        FPrimaryAssetId() = default;
        explicit FPrimaryAssetId(const FName& InName) : Name(InName) {}

        bool        IsValid() const { return !Name.IsNone(); }
        const FName& GetName() const { return Name; }

        bool operator==(const FPrimaryAssetId& Other) const { return Name == Other.Name; }
        bool operator!=(const FPrimaryAssetId& Other) const { return Name != Other.Name; }

        friend RUNTIME_API FArchive& operator<<(FArchive& Ar, FPrimaryAssetId& Self);

    protected:

        FName Name;
    };


    /** Typed wrapper; narrows resolve/load results back to T*. */
    template<typename T>
    struct TPrimaryAssetId : FPrimaryAssetId
    {
        TPrimaryAssetId() = default;
        explicit TPrimaryAssetId(const FName& InName) : FPrimaryAssetId(InName) {}
        explicit TPrimaryAssetId(const FPrimaryAssetId& InBase) : FPrimaryAssetId(InBase) {}

        TObjectPtr<T> LoadSynchronous() const;
        void          LoadAsync(const TFunction<void(T*)>& Callback) const;
    };
}

// TPrimaryAssetId::LoadSynchronous/LoadAsync impls live in AssetManager.h
// (template definitions need the full FAssetManager type, which would create
// a circular include if defined here).

namespace eastl
{
    template<>
    struct hash<Lumina::FPrimaryAssetId>
    {
        size_t operator()(const Lumina::FPrimaryAssetId& P) const noexcept
        {
            return eastl::hash<Lumina::FName>{}(P.GetName());
        }
    };
}
