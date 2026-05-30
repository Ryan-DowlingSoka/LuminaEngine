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

		// Walks engine/project/plugin content, extracting FAssetData (incl. Dependencies) per .lasset.
		// Incremental via .assetdb cache (mtime + content hash); async on Task::AsyncTask.
		void RunInitialDiscovery();
		void OnInitialDiscoveryCompleted();

		void AssetCreated(const CObject* Asset);
		void AssetDeleted(const FGuid& GUID);
		void AssetRenamed(FStringView OldPath, FStringView NewPath);
		void AssetSaved(CObject* Asset);

		FAssetData* GetAssetByGUID(const FGuid& GUID) const;
		FAssetData* GetAssetByPath(FStringView Path) const;
		TVector<FAssetData*> FindByPredicate(const TFunction<bool(const FAssetData&)>& Predicate);

		// Every asset listing GUID in its Dependencies; O(1) avg via a reverse map built lazily after change.
		TVector<FAssetData*> GetReferencersOf(const FGuid& GUID) const;

		// Serialize the live registry (same wire format as the .assetdb cache); the cooker bundles a
		// pre-baked registry into the PAK so the runtime skips the filesystem rescan at start.
		void WriteToArchive(FArchive& Ar) const;
		bool LoadFromArchive(FArchive& Ar);

		FAssetRegistryUpdatedDelegate& GetOnAssetRegistryUpdated() { return OnAssetRegistryUpdated; }

		const FAssetDataMap& GetAssets() const { return Assets; }
		const TVector<FString>& GetFailedAssets() const { return FailedAssets; }

	private:

		// True iff the on-disk asset is new or changed since the cached entry was extracted.
		bool NeedsReextract(FStringView Path, int64 MTimeNs, uint64 ContentHash) const;

		// Full parse: header + ImportTable -> FAssetData incl. Dependencies.
		void ProcessPackagePath(FStringView Path);

		void ClearAssets();

		void BroadcastRegistryUpdate();

		void RecordFailedAsset(FStringView Path);

		// Persistence to <EngineInstall>/Intermediates/AssetRegistry.assetdb; stale entries re-validated on next discovery.
		void SaveCache() const;
		bool LoadCache();

		// (Re)build the reverse-dep map from current Assets. Cheap O(N*avgDeps).
		void RebuildReverseMap();

		// Reap cached entries under a walked root not visited this discovery (externally deleted).
		// Disabled-plugin content survives: its mount isn't a walked root.
		void ReapStaleEntries();


		FAssetRegistryUpdatedDelegate	OnAssetRegistryUpdated;

		mutable FSharedMutex			AssetsMutex;
		FAssetDataMap 					Assets;

		mutable FSharedMutex			FailedAssetsMutex;
		TVector<FString>				FailedAssets;

		mutable FSharedMutex			ReverseMapMutex;
		THashMap<FGuid, TVector<FGuid>, FGuidHash> ReverseDepMap; // dep -> referrers
		mutable bool					bReverseMapDirty = true;

		// Snapshot from the most recent RunInitialDiscovery, consumed by
		// OnInitialDiscoveryCompleted to drive the reap pass.
		TVector<FFixedString>			LastDiscoveryWalkedRoots;
		TVector<FFixedString>			LastDiscoveryVisitedPaths; // sorted
	};

}
