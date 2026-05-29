#pragma once

#include "Containers/Function.h"
#include "Containers/String.h"
#include "GUID/GUID.h"
#include "ObjectHandleTyped.h"


namespace Lumina
{
    class CObject;
    class FArchive;

    /** Serializable reference to an asset by path. Does NOT force-load the
     *  target — resolves to a CObject* on demand via FAssetManager. The GUID
     *  is cached the first time the path is resolved through FAssetRegistry
     *  so subsequent lookups don't re-walk the registry.
     *
     *  Cook-graph wiring (Phase 2B): the package serializer emits the
     *  CachedGUID into the ImportTable tagged as EDependencyType::Soft, so
     *  FCookGraph follows it for reachability but doesn't force the target
     *  into the referrer's chunk. */
    struct RUNTIME_API FSoftObjectPath
    {
        FSoftObjectPath() = default;
        explicit FSoftObjectPath(FStringView InPath) : Path(InPath.data(), InPath.size()) {}
        explicit FSoftObjectPath(const FGuid& InGUID) : CachedGUID(InGUID) {}
        FSoftObjectPath(FStringView InPath, const FGuid& InGUID)
            : Path(InPath.data(), InPath.size())
            , CachedGUID(InGUID)
        {}

        bool        IsNull() const  { return Path.empty() && !CachedGUID.IsValid(); }
        bool        IsValid() const { return !IsNull(); }

        FStringView GetPath() const { return FStringView(Path.c_str(), Path.size()); }
        const FGuid& GetCachedGUID() const { return CachedGUID; }

        void SetPath(FStringView InPath)
        {
            Path.assign(InPath.data(), InPath.size());
            CachedGUID = FGuid();
        }

        /** Look up Path in FAssetRegistry to populate CachedGUID. No-op if
         *  GUID already known. Returns true on success. */
        bool TryResolve() const;

        /** Blocking load through FAssetManager; returns nullptr on miss. */
        CObject* LoadSynchronous() const;

        /** Fire-and-forget async load. Callback fires on completion (or
         *  immediately with nullptr if the path can't be resolved). */
        void LoadAsync(const TFunction<void(CObject*)>& Callback) const;

        bool operator==(const FSoftObjectPath& Other) const
        {
            // Identity by GUID when both resolved; else by path text.
            if (CachedGUID.IsValid() && Other.CachedGUID.IsValid())
                return CachedGUID == Other.CachedGUID;
            return Path == Other.Path;
        }
        bool operator!=(const FSoftObjectPath& Other) const { return !(*this == Other); }

        friend RUNTIME_API FArchive& operator<<(FArchive& Ar, FSoftObjectPath& Self);

    private:
        FFixedString    Path;
        mutable FGuid   CachedGUID;
    };


    /** Typed soft pointer. Pure wrapper around FSoftObjectPath that narrows
     *  Get/Load results back to T*. */
    template<typename T>
    struct TSoftObjectPtr
    {
        TSoftObjectPtr() = default;
        explicit TSoftObjectPtr(FStringView InPath) : Inner(InPath) {}
        explicit TSoftObjectPtr(const FSoftObjectPath& InPath) : Inner(InPath) {}
        explicit TSoftObjectPtr(const FGuid& InGUID) : Inner(InGUID) {}

        bool        IsNull() const  { return Inner.IsNull(); }
        bool        IsValid() const { return Inner.IsValid(); }

        FStringView           GetPath() const        { return Inner.GetPath(); }
        const FGuid&          GetCachedGUID() const  { return Inner.GetCachedGUID(); }
        const FSoftObjectPath& GetSoftPath() const   { return Inner; }
        FSoftObjectPath&      GetSoftPath()          { return Inner; }

        TObjectPtr<T> LoadSynchronous() const
        {
            CObject* Obj = Inner.LoadSynchronous();
            return TObjectPtr<T>(static_cast<T*>(Obj));
        }

        void LoadAsync(const TFunction<void(T*)>& Callback) const
        {
            Inner.LoadAsync([Callback](CObject* Obj)
            {
                if (Callback) Callback(static_cast<T*>(Obj));
            });
        }

        bool operator==(const TSoftObjectPtr& Other) const { return Inner == Other.Inner; }
        bool operator!=(const TSoftObjectPtr& Other) const { return Inner != Other.Inner; }

        friend FArchive& operator<<(FArchive& Ar, TSoftObjectPtr& Self) { return Ar << Self.Inner; }

    private:
        FSoftObjectPath Inner;
    };
}

namespace eastl
{
    template<>
    struct hash<Lumina::FSoftObjectPath>
    {
        size_t operator()(const Lumina::FSoftObjectPath& P) const noexcept
        {
            if (P.GetCachedGUID().IsValid())
            {
                return eastl::hash<Lumina::FGuid>{}(P.GetCachedGUID());
            }
            const Lumina::FStringView V = P.GetPath();
            return eastl::hash<eastl::string_view>{}(eastl::string_view(V.data(), V.size()));
        }
    };

    template<typename T>
    struct hash<Lumina::TSoftObjectPtr<T>>
    {
        size_t operator()(const Lumina::TSoftObjectPtr<T>& P) const noexcept
        {
            return eastl::hash<Lumina::FSoftObjectPath>{}(P.GetSoftPath());
        }
    };
}
