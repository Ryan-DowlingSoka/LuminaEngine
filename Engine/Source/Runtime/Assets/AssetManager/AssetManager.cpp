#include "pch.h"

#include "AssetManager.h"
#include "Assets/AssetRegistry/AssetData.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Log/Log.h"
#include "TaskSystem/TaskSystem.h"

namespace Lumina
{
    FAssetManager& FAssetManager::Get()
    {
        static FAssetManager Instance;
        return Instance;
    }
    
    TSharedPtr<FAssetRequest> FAssetManager::LoadAssetAsync(const FFixedString& PackagePath, const FGuid& RequestedAsset)
    {
        bool bAlreadyInQueue = false;
        TSharedPtr<FAssetRequest> ActiveRequest = CreateOrFindAssetRequest(PackagePath, RequestedAsset, bAlreadyInQueue);
        
        if (!bAlreadyInQueue)
        {
            SubmitAssetRequest(ActiveRequest);
        }

        return ActiveRequest;
    }

    CObject* FAssetManager::LoadAssetSynchronous(const FFixedString& PackagePath, const FGuid& RequestedAsset)
    {
        bool bAlreadyInQueue = false;
        TSharedPtr<FAssetRequest> ActiveRequest = CreateOrFindAssetRequest(PackagePath, RequestedAsset, bAlreadyInQueue);
        
        if (bAlreadyInQueue)
        {
            return FindObject<CObject>(ActiveRequest->RequestedGUID);
        }
        
        ActiveRequest->Process();
        NotifyAssetRequestCompleted(ActiveRequest);
        
        return ActiveRequest->GetPendingObject();
    }

    void FAssetManager::NotifyAssetRequestCompleted(const TSharedPtr<FAssetRequest>& Request)
    {
        auto It = eastl::find(ActiveRequests.begin(), ActiveRequests.end(), Request);
        ASSERT(It != ActiveRequests.end());

        for (auto& Functor : Request->Listeners)
        {
            Functor(Request->PendingObject);
        }
        
        ActiveRequests.erase(It);
    }

    void FAssetManager::FlushAsyncLoading()
    {

    }

    FAssetData* FAssetManager::ResolvePrimaryAsset(const FPrimaryAssetId& Id) const
    {
        if (!Id.IsValid())
        {
            return nullptr;
        }

        const FName Target = Id.GetName();
        FAssetData* Hit = nullptr;
        const TVector<FAssetData*> Candidates = FAssetRegistry::Get().FindByPredicate(
            [&](const FAssetData& D)
            {
                return HasFlag(D.Flags, EAssetFlags::Primary) && D.AssetName == Target;
            });

        if (Candidates.empty())
        {
            return nullptr;
        }
        if (Candidates.size() > 1)
        {
            LOG_WARN("FAssetManager: primary id '{}' resolves to {} assets; returning the first. Primary names must be unique.",
                Target.ToString(), Candidates.size());
        }
        return Candidates[0];
    }

    CObject* FAssetManager::LoadPrimaryAssetSynchronous(const FPrimaryAssetId& Id)
    {
        FAssetData* Data = ResolvePrimaryAsset(Id);
        if (Data == nullptr) return nullptr;
        return LoadAssetSynchronous(Data->Path, Data->AssetGUID);
    }

    TSharedPtr<FAssetRequest> FAssetManager::LoadPrimaryAssetAsync(const FPrimaryAssetId& Id)
    {
        FAssetData* Data = ResolvePrimaryAsset(Id);
        if (Data == nullptr) return {};
        return LoadAssetAsync(Data->Path, Data->AssetGUID);
    }

    TSharedPtr<FAssetRequest> FAssetManager::CreateOrFindAssetRequest(const FFixedString& InAssetPath, const FGuid& GUID, bool& bAlreadyInQueue)
    {
        FScopeLock Lock(RequestMutex);
        
        auto It = eastl::find_if(ActiveRequests.begin(), ActiveRequests.end(), [&](const TSharedPtr<FAssetRequest>& Request)
        {
            return Request->GetAssetPath() == InAssetPath && Request->RequestedGUID == GUID;
        });

        if (It != ActiveRequests.end())
        {
            bAlreadyInQueue = true;
            return *It;
        }

        bAlreadyInQueue = false;
        TSharedPtr<FAssetRequest> NewRequest = MakeShared<FAssetRequest>(InAssetPath, GUID, nullptr);
        return ActiveRequests.emplace_back(NewRequest);
    }

    void FAssetManager::SubmitAssetRequest(const TSharedPtr<FAssetRequest>& Request)
    {
        Task::AsyncTask(1, 1, [this, Request](uint32, uint32, uint32)
        {
            Request->Process();
            NotifyAssetRequestCompleted(Request);
        });
    }
    
}

