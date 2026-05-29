#include "pch.h"
#include "SoftObjectPtr.h"

#include "Assets/AssetManager/AssetManager.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRequest.h"
#include "Core/Serialization/Archiver.h"


namespace Lumina
{
    bool FSoftObjectPath::TryResolve() const
    {
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

        CachedGUID = Data->AssetGUID;
        return true;
    }

    CObject* FSoftObjectPath::LoadSynchronous() const
    {
        if (!TryResolve())
        {
            return nullptr;
        }
        return FAssetManager::Get().LoadAssetSynchronous(Path, CachedGUID);
    }

    void FSoftObjectPath::LoadAsync(const TFunction<void(CObject*)>& Callback) const
    {
        if (!TryResolve())
        {
            if (Callback) Callback(nullptr);
            return;
        }
        TSharedPtr<FAssetRequest> Request = FAssetManager::Get().LoadAssetAsync(Path, CachedGUID);
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
