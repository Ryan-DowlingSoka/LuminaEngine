#pragma once

#include "Containers/String.h"
#include "Core/Object/Object.h"
#include "TaskSystem/TaskSystem.h"

namespace Lumina
{
    class CObject;

    class FAssetRequest
    {
    public:

        friend class FAssetManager;

        
        FAssetRequest(const FFixedString& InPath, const FGuid& GUID, const FTaskHandle& InTask)
            : AssetPath(InPath)
            , RequestedGUID(GUID)
            , Task(InTask)
            , PendingObject(nullptr)
        {
        }

        FStringView GetAssetPath() const { return AssetPath; }
        CObject* GetPendingObject() const { return PendingObject; }
        void AddListener(const TFunction<void(CObject*)>& Functor) { Listeners.push_back(Functor); }
        void Wait() const { return Task->Wait(); }
    
    private:

        bool Process();
        
    private:

        TVector<TFunction<void(CObject*)>>  Listeners;
        FFixedString                        AssetPath;
        FGuid                               RequestedGUID;
        FTaskHandle                         Task;
        CObject*                            PendingObject;
    };
    
}
