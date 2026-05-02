#pragma once

#include "Assets/AssetRequest.h"
#include "Containers/Array.h"
#include "Memory/SmartPtr.h"


namespace Lumina
{
	class FAssetRecord;
	
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
		
	private:

		TSharedPtr<FAssetRequest> CreateOrFindAssetRequest(const FFixedString& InAssetPath, const FGuid& GUID, bool& bAlreadyInQueue);
		void SubmitAssetRequest(const TSharedPtr<FAssetRequest>& Request);
		
	
	private:

		FMutex								RequestMutex;
		TVector<TSharedPtr<FAssetRequest>>	ActiveRequests;

	};

}
