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

		// Walks the engine + project + plugin content trees, extracts
		// FAssetData (incl. Dependencies) for every .lasset. Skips unchanged
		// entries on incremental passes via .assetdb cache (mtime + content
		// hash). Async via Task::AsyncTask.
		void RunInitialDiscovery();
		void OnInitialDiscoveryCompleted();

		void AssetCreated(const CObject* Asset);
		void AssetDeleted(const FGuid& GUID);
		void AssetRenamed(FStringView OldPath, FStringView NewPath);
		void AssetSaved(CObject* Asset);

		FAssetData* GetAssetByGUID(const FGuid& GUID) const;
		FAssetData* GetAssetByPath(FStringView Path) const;
		TVector<FAssetData*> FindByPredicate(const TFunction<bool(const FAssetData&)>& Predicate);

		// Reverse-dep query: every asset that lists the given GUID in its
		// Dependencies. O(1) average via the precomputed reverse map.
		// Built lazily on first call after registry change.
		TVector<FAssetData*> GetReferencersOf(const FGuid& GUID) const;

		// Serialize the live registry to/from a binary archive. Same wire
		// format as the on-disk .assetdb cache; the cooker uses these to
		// bundle a pre-baked registry into the shipped PAK so the runtime
		// can skip the full filesystem rescan at game start.
		void WriteToArchive(FArchive& Ar) const;
		bool LoadFromArchive(FArchive& Ar);

		FAssetRegistryUpdatedDelegate& GetOnAssetRegistryUpdated() { return OnAssetRegistryUpdated; }

		const FAssetDataMap& GetAssets() const { return Assets; }
		const TVector<FString>& GetFailedAssets() const { return FailedAssets; }

	private:

		// Returns true iff the asset on disk is new or has changed since
		// the cached entry was extracted; ProcessPackagePath then re-parses.
		bool NeedsReextract(FStringView Path, int64 MTimeNs, uint64 ContentHash) const;

		// Full parse: header + ImportTable -> FAssetData incl. Dependencies.
		// The caller decides whether to insert/replace based on NeedsReextract.
		void ProcessPackagePath(FStringView Path);

		void ClearAssets();

		void BroadcastRegistryUpdate();

		void RecordFailedAsset(FStringView Path);

		// Persistence to <EngineInstall>/Intermediates/AssetRegistry.assetdb.
		// Binary, FArchive-serialized; tolerant of stale entries (caller
		// re-validates each on next discovery via NeedsReextract).
		void SaveCache() const;
		bool LoadCache();

		// (Re)build the reverse-dep map from current Assets. Cheap O(N*avgDeps).
		void RebuildReverseMap();

		// Reap cached entries whose path is under a walked root but whose
		// file wasn't visited this discovery (i.e. externally deleted).
		// Disabled-plugin content is preserved — its mount isn't a walked root.
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
