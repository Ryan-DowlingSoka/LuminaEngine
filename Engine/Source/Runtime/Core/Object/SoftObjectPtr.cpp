#include "pch.h"
#include "SoftObjectPtr.h"

#include "Assets/AssetManager/AssetManager.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRequest.h"
#include "Core/Serialization/Archiver.h"

#include <mutex>


namespace Lumina
{
    namespace
    {
        // Global mutex guarding the rare "first resolve" GUID write on any
        // FSoftObjectPath. Per-path mutexes would balloon the size of
        // every soft pointer; this single lock is only contended during
        // the brief registry lookup that happens at most once per (path,
        // process) pair. Hot reads (CachedGUID already set) skip locking
        // via the fast path below.
        std::mutex& ResolveMutex()
        {
            static std::mutex M;
            return M;
        }
    }

    bool FSoftObjectPath::TryResolve() const
    {
        // Lock-free fast path: once any thread has populated CachedGUID,
        // every subsequent caller observes it without touching the mutex.
        if (CachedGUID.IsValid())
        {
            return true;
        }
        if (Path.empty())
        {
            return false;
        }

        FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(FStringView(Path.c_str(), Path.size()));
        if (Data == nullptr)
        {
            return false;
        }

        // Serialise the GUID write so concurrent first-resolvers can't
        // tear the 16-byte FGuid. The double-check inside the lock means
        // we don't re-do the assignment if a peer beat us to it.
        std::lock_guard<std::mutex> Lock(ResolveMutex());
        if (!CachedGUID.IsValid())
        {
            CachedGUID = Data->AssetGUID;
        }
        return true;
    }

    CObject* FSoftObjectPath::LoadSynchronous() const
    {
        if (!TryResolve())
        {
            return nullptr;
        }
        // AssetManager API takes FFixedString; soft paths are FString so
        // they can hold arbitrarily-deep package paths without truncation.
        // The bridge construction here is the only narrowing; the load
        // itself rejects oversize paths via its own bounds check.
        return FAssetManager::Get().LoadAssetSynchronous(
            FFixedString(Path.c_str(), Path.size()), CachedGUID);
    }

    void FSoftObjectPath::LoadAsync(const TFunction<void(CObject*)>& Callback) const
    {
        if (!TryResolve())
        {
            if (Callback) Callback(nullptr);
            return;
        }
        TSharedPtr<FAssetRequest> Request = FAssetManager::Get().LoadAssetAsync(
            FFixedString(Path.c_str(), Path.size()), CachedGUID);
        if (!Request)
        {
            if (Callback) Callback(nullptr);
            return;
        }
        if (Callback) Request->AddListener(Callback);
    }

    FArchive& operator<<(FArchive& Ar, FSoftObjectPath& Self)
    {
        // On-disk: path string + cached GUID. On write, resolve the path
        // first so the persisted GUID is current, then nudge the archive's
        // soft-ref hook so package savers can fold the GUID into their
        // ImportTable as a Soft edge for the cook graph.
        if (Ar.IsWriting() && !Self.Path.empty() && !Self.CachedGUID.IsValid())
        {
            Self.TryResolve();
        }

        Ar << Self.Path;
        Ar << Self.CachedGUID;

        if (Ar.IsWriting() && Self.CachedGUID.IsValid())
        {
            Ar.RegisterSoftAssetReference(Self.CachedGUID);
        }
        return Ar;
    }
}
