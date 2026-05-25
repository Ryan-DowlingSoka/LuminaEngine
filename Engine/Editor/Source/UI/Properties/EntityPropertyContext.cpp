#include "EntityPropertyContext.h"

namespace Lumina
{
    namespace
    {
        // Active only while a property table is being drawn; nested DrawTree calls
        // (e.g. struct properties) inherit it, save/restore keeps them balanced.
        CWorld* GActiveEntityPropertyWorld = nullptr;
    }

    CWorld* GetEntityPropertyContextWorld()
    {
        return GActiveEntityPropertyWorld;
    }

    FScopedEntityPropertyContext::FScopedEntityPropertyContext(CWorld* InWorld)
        : Previous(GActiveEntityPropertyWorld)
    {
        GActiveEntityPropertyWorld = InWorld;
    }

    FScopedEntityPropertyContext::~FScopedEntityPropertyContext()
    {
        GActiveEntityPropertyWorld = Previous;
    }
}
