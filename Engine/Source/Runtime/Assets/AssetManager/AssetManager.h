#pragma once

#include "Assets/AssetRequest.h"
#include "Containers/Array.h"
#include "Memory/SmartPtr.h"
#include "PrimaryAssetId.h"


namespace Lumina
{
	class FAssetRecord;
	struct FAssetData;

	class RUNTIME_API FAssetManager final
	{
	public:
		FAssetManager() = default;
		LE_NO_COPYMOVE(FAssetManager);


		static FAssetManager& Get();

		TSharedPtr<FAssetRequest> LoadAssetAsync(const FFixedString& PackagePath, const FGuid& RequestedAsset);
		CObject* LoadAssetSynchronous(const FFixedString& PackagePath, const FGuid& RequestedAsset);

		void NotifyAssetRequestCompleted(const TSharedPtr<FAssetRequest>& Request);

		void FlushAsyncLoading();

		// Primary-asset lookup. Searches FAssetRegistry for an asset whose
		// AssetName == Id.Name AND has EAssetFlags::Primary set. Returns
		// the FAssetData* on hit, nullptr on miss.
		FAssetData* ResolvePrimaryAsset(const FPrimaryAssetId& Id) const;

		// Sync load by primary id. Convenience over ResolvePrimaryAsset +
		// LoadAssetSynchronous; nullptr if no Primary asset is registered
		// under that name.
		CObject* LoadPrimaryAssetSynchronous(const FPrimaryAssetId& Id);

		// Async load by primary id. Callback fires with nullptr if the id
		// doesn't resolve.
		TSharedPtr<FAssetRequest> LoadPrimaryAssetAsync(const FPrimaryAssetId& Id);

		template<typename T>
		TObjectPtr<T> LoadPrimaryAssetSynchronous(const TPrimaryAssetId<T>& Id)
		{
			return TObjectPtr<T>(static_cast<T*>(LoadPrimaryAssetSynchronous(static_cast<const FPrimaryAssetId&>(Id))));
		}

	private:

		TSharedPtr<FAssetRequest> CreateOrFindAssetRequest(const FFixedString& InAssetPath, const FGuid& GUID, bool& bAlreadyInQueue);
		void SubmitAssetRequest(const TSharedPtr<FAssetRequest>& Request);
		
	
	private:

		FMutex								RequestMutex;
		TVector<TSharedPtr<FAssetRequest>>	ActiveRequests;

	};


	// TPrimaryAssetId<T> templated impls — out-of-line here because they
	// need the full FAssetManager declaration above.

	template<typename T>
	TObjectPtr<T> TPrimaryAssetId<T>::LoadSynchronous() const
	{
		CObject* Obj = FAssetManager::Get().LoadPrimaryAssetSynchronous(static_cast<const FPrimaryAssetId&>(*this));
		return TObjectPtr<T>(static_cast<T*>(Obj));
	}

	template<typename T>
	void TPrimaryAssetId<T>::LoadAsync(const TFunction<void(T*)>& Callback) const
	{
		TSharedPtr<FAssetRequest> Request = FAssetManager::Get().LoadPrimaryAssetAsync(static_cast<const FPrimaryAssetId&>(*this));
		if (!Request)
		{
			if (Callback) Callback(nullptr);
			return;
		}
		if (Callback)
		{
			Request->AddListener([Callback](CObject* Obj)
			{
				Callback(static_cast<T*>(Obj));
			});
		}
	}
}
