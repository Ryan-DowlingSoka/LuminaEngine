#include "pch.h"
#include "ScriptPropertyOverrides.h"

#include "Core/Serialization/Archiver.h"

namespace Lumina
{
    bool FScriptPropertyOverrides::Serialize(FArchive& Ar)
    {
        constexpr uint8 CurrentVersion = 1;
        uint8 Version = CurrentVersion;
        Ar << Version;

        if (Ar.IsReading() && Version != CurrentVersion)
        {
            Items.clear();
            Ar.SetHasError(true);
            return false;
        }

        uint32 Count = (uint32)Items.size();
        Ar << Count;

        if (Ar.IsReading())
        {
            if (Count > Ar.GetMaxSerializeSize())
            {
                Items.clear();
                Ar.SetHasError(true);
                return false;
            }
            Items.clear();
            Items.resize(Count);
        }

        // If a single item fails its own forward-compat skip can resync the stream;
        // reconcile will replace Nil/mismatched entries against the live schema.
        for (uint32 i = 0; i < Count; ++i)
        {
            Ar << Items[i].Name;
            Items[i].Value.Serialize(Ar);
        }

        return !Ar.HasError();
    }
}
