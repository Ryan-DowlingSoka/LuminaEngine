#include "pch.h"
#include "ObjectHash.h"

#include "ObjectBase.h"
#include "Package/Package.h"

namespace Lumina
{
    void FObjectHashTables::AddObject(CObjectBase* Object)
    {
        LUMINA_PROFILE_SCOPE();
        FWriteScopeLock Lock(Mutex);

        const FGuid& ObjectGUID = Object->GetGUID();
        auto It = ObjectGUIDHash.find(ObjectGUID);
        ASSERT(It == ObjectGUIDHash.end());

        ObjectGUIDHash.emplace(ObjectGUID, Object);
        ObjectClassBucket[Object->GetClass()].emplace(Object);
    }

    void FObjectHashTables::RemoveObject(CObjectBase* Object)
    {
        LUMINA_PROFILE_SCOPE();
        FWriteScopeLock Lock(Mutex);

        const FGuid& ObjectGUID = Object->GetGUID();
        auto It = ObjectGUIDHash.find(ObjectGUID);
        ASSERT(It != ObjectGUIDHash.end());

        ObjectGUIDHash.erase(It);


        ObjectClassBucket[Object->GetClass()].erase(Object);
    }

    CObjectBase* FObjectHashTables::FindObject(const FGuid& GUID)
    {
        LUMINA_PROFILE_SCOPE();
        FReadScopeLock Lock(Mutex);

        auto It = ObjectGUIDHash.find(GUID);
        if (It == ObjectGUIDHash.end())
        {
            return nullptr;
        }

        // A package/object marked for destroy is logically gone even though it
        // lingers in the table until DrainPendingDestroys frees it.
        if (It->second->HasAnyFlag(OF_MarkedDestroy))
        {
            return nullptr;
        }

        return It->second;
    }

    CObjectBase* FObjectHashTables::FindObject(const FName& Name, CClass* Class)
    {
        LUMINA_PROFILE_SCOPE();
        FReadScopeLock Lock(Mutex);

        auto It = ObjectClassBucket.find(Class);
        if (It == ObjectClassBucket.end())
        {
            return nullptr;
        }

        for (CObjectBase* Object : It->second)
        {
            if (Object->GetName() == Name && !Object->HasAnyFlag(OF_MarkedDestroy))
            {
                return Object;
            }
        }

        return nullptr;
    }


    void FObjectHashTables::Clear()
    {
        FWriteScopeLock Lock(Mutex);
        ObjectGUIDHash.clear();
    }
}
