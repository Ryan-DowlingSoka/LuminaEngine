#pragma once

#include "PakFile.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"

namespace Lumina
{
    /**
     * Builds a .pak by buffering (virtualPath, bytes) entries in memory and
     * writing the final file in one shot. Currently in-memory; if PAK sizes
     * grow large enough to matter we can swap for a streaming-to-disk impl.
     */
    class RUNTIME_API FPakWriter
    {
    public:

        // The path is stored verbatim — pass full virtual paths like
        // "/Game/Content/Foo.lasset". Returns false if Path was already added.
        bool AddEntry(FStringView VirtualPath, TSpan<const uint8> Data);
        bool AddEntry(FStringView VirtualPath, FStringView Data);

        // Writes the pak to NativeFilePath. Overwrites if it exists.
        bool Finalize(FStringView NativeFilePath);

        size_t NumEntries() const { return Entries.size(); }
        size_t TotalEntryBytes() const { return TotalDataSize; }

    private:

        struct FPendingEntry
        {
            FFixedString    VirtualPath;
            TVector<uint8>  Data;
        };

        TVector<FPendingEntry>          Entries;
        THashSet<FFixedString>          SeenPaths;
        size_t                          TotalDataSize = 0;
    };
}
