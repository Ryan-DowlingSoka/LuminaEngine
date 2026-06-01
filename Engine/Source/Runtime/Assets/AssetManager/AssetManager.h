#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "GUID/GUID.h"
#include "PrimaryAssetId.h"
#include "TaskSystem/FiberSync.h"
#include "TaskSystem/Future.h"


namespace Lumina
{
	class CObject;
	struct FAssetData;

	// Result of an async load: a fiber-aware future for the loaded object (nullptr on failure). Poll it
	// (IsReady), block on it (Get / Wait, parks the calling fiber, never stalls a worker), or chain a
	// callback off it (Then). A default-constructed handle is invalid (the request didn't resolve).
	using FAssetHandle = TFuture<CObject*>;

	// Loads objects from packages, deduplicating concurrent requests for the same asset. Sync and async
	// loads share one path: both hand back the same shared future, so the only difference is whether the
	// caller performs the load inline (sync) or lets a worker do it (async), and then waits or not.
	class RUNTIME_API FAssetManager final
	{
	public:
		FAssetManager() = default;
		LE_NO_COPYMOVE(FAssetManager);

		static FAssetManager& Get();

		// Kick off (or join) an async load. Returns the shared handle for this asset.
		FAssetHandle LoadAssetAsync(const FFixedString& PackagePath, const FGuid& RequestedAsset);
		// Load now and return the object. Joins an in-flight async load (fiber-aware wait) if one exists,
		// otherwise loads inline on the caller. nullptr on failure.
		CObject*     LoadAssetSynchronous(const FFixedString& PackagePath, const FGuid& RequestedAsset);

		// Block until every in-flight async load has finished. Fiber-aware.
		void FlushAsyncLoading();

		// Finds the asset whose AssetName == Id.Name with EAssetFlags::Primary set; nullptr on miss.
		FAssetData*  ResolvePrimaryAsset(const FPrimaryAssetId& Id) const;

		CObject*     LoadPrimaryAssetSynchronous(const FPrimaryAssetId& Id);
		// Returns an invalid handle if the id doesn't resolve to a Primary asset.
		FAssetHandle LoadPrimaryAssetAsync(const FPrimaryAssetId& Id);

		template<typename T>
		TObjectPtr<T> LoadPrimaryAssetSynchronous(const TPrimaryAssetId<T>& Id)
		{
			return TObjectPtr<T>(static_cast<T*>(LoadPrimaryAssetSynchronous(static_cast<const FPrimaryAssetId&>(Id))));
		}

	private:

		// One pass over the in-flight table. If this asset is already loading, returns its shared handle
		// and leaves bShouldLoad false. Otherwise registers a fresh handle, hands its promise to the
		// caller (via OutPromise), and sets bShouldLoad true, the caller then performs the load.
		FAssetHandle AcquireLoad(const FGuid& GUID, TPromise<CObject*>& OutPromise, bool& bShouldLoad);
		// Load the object from its package, fulfill the promise, and retire the in-flight entry.
		void         PerformLoad(const FFixedString& Path, const FGuid& GUID, TPromise<CObject*> Promise);

	private:

		FFiberMutex                          RequestMutex;
		THashMap<FGuid, FAssetHandle>        InFlight; // asset GUID -> shared load handle, while loading
	};


	// TPrimaryAssetId<T> templated impls, out-of-line here because they need the full FAssetManager.

	template<typename T>
	TObjectPtr<T> TPrimaryAssetId<T>::LoadSynchronous() const
	{
		CObject* Obj = FAssetManager::Get().LoadPrimaryAssetSynchronous(static_cast<const FPrimaryAssetId&>(*this));
		return TObjectPtr<T>(static_cast<T*>(Obj));
	}

	template<typename T>
	void TPrimaryAssetId<T>::LoadAsync(const TFunction<void(T*)>& Callback) const
	{
		FAssetHandle Handle = FAssetManager::Get().LoadPrimaryAssetAsync(static_cast<const FPrimaryAssetId&>(*this));
		if (!Handle.IsValid())
		{
			if (Callback) Callback(nullptr);
			return;
		}
		if (Callback)
		{
			Handle.Then([Callback](CObject*& Obj) { Callback(static_cast<T*>(Obj)); });
		}
	}
}
