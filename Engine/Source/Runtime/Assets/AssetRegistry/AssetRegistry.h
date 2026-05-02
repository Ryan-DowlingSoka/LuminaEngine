#pragma once

#include "AssetData.h"
#include "Core/Delegates/Delegate.h"
#include "Core/Threading/Thread.h"
#include "Memory/SmartPtr.h"


DECLARE_MULTICAST_DELEGATE(FAssetRegistryUpdatedDelegate);

namespace Lumina
{
	struct FObjectExport;
	struct FPackageHeader;
	class CPackage;
	struct FAssetData;
	class CClass;

	struct FAssetDataPtrHash
	{
		size_t operator() (const TUniquePtr<FAssetData>& Asset) const noexcept
		{
			return Hash::GetHash(Asset->AssetGUID);
		}
	};

	struct FAssetDataPtrEqual
	{
		bool operator()(const TUniquePtr<FAssetData>& A, const TUniquePtr<FAssetData>& B) const noexcept
		{
			return A->AssetGUID == B->AssetGUID;
		}
	};

	struct FGuidHash
	{
		size_t operator()(const FGuid& GUID) const noexcept
		{
			return Hash::GetHash(GUID);
		}
	};

	struct FAssetDataGuidEqual
	{
		bool operator()(const TUniquePtr<FAssetData>& Asset, const FGuid& GUID) const noexcept
		{
			return Asset->AssetGUID == GUID;
		}
	};

	using FAssetDataMap = THashSet<TUniquePtr<FAssetData>, FAssetDataPtrHash, FAssetDataPtrEqual>;


	class RUNTIME_API FAssetRegistry final
	{
	public:

		static FAssetRegistry& Get();
		
		void RunInitialDiscovery();
		void OnInitialDiscoveryCompleted();

		void AssetCreated(const CObject* Asset);
		void AssetDeleted(const FGuid& GUID);
		void AssetRenamed(FStringView OldPath, FStringView NewPath);
		void AssetSaved(CObject* Asset);

		FAssetData* GetAssetByGUID(const FGuid& GUID) const;
		FAssetData* GetAssetByPath(FStringView Path) const;
		TVector<FAssetData*> FindByPredicate(const TFunction<bool(const FAssetData&)>& Predicate);

		FAssetRegistryUpdatedDelegate& GetOnAssetRegistryUpdated() { return OnAssetRegistryUpdated; }

		const FAssetDataMap& GetAssets() const { return Assets; }
		const TVector<FString>& GetFailedAssets() const { return FailedAssets; }

	private:

		void ProcessPackagePath(FStringView Path);

		void ClearAssets();

		void BroadcastRegistryUpdate();

		void RecordFailedAsset(FStringView Path);


		FAssetRegistryUpdatedDelegate	OnAssetRegistryUpdated;

		mutable FSharedMutex			AssetsMutex;
		FAssetDataMap 					Assets;

		mutable FSharedMutex			FailedAssetsMutex;
		TVector<FString>				FailedAssets;
	};

}
