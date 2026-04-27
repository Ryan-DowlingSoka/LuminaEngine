#pragma once

#include "PakFile.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    /**
     * Loaded representation of a .pak file. Holds the raw blob and a TOC map
     * keyed by full virtual path. Multiple FPakFileSystem mounts can share
     * one archive via TSharedPtr to avoid duplicating the buffer.
     *
     * Open() reads the entire file into memory in one shot. For shippable
     * builds this is fine — Godot does the same and it's far simpler than
     * managing an mmap or per-entry seeks. Compression / mmap are future work.
     */
    class RUNTIME_API FPakArchive
    {
    public:

        // Returns nullptr if the file is missing, malformed, or has the wrong magic.
        static TSharedPtr<FPakArchive> Open(FStringView NativeFilePath);

        // Look up an entry by full virtual path (e.g. "/Game/Content/Foo.lasset").
        // Returns an empty span if missing.
        TSpan<const uint8> ReadEntry(FStringView VirtualPath) const;

        bool   HasEntry(FStringView VirtualPath) const;
        size_t EntrySize(FStringView VirtualPath) const;
        size_t NumEntries() const { return Entries.size(); }

        // Set of unique top-level path components — i.e. "/Engine", "/Game",
        // "/Config" derived from the entries. The cooked-runtime boot mounts
        // one FPakFileSystem per entry here.
        TVector<FString> GetTopLevelAliases() const;

        // Entry iteration (for VFS DirectoryIterator). The callback receives
        // the entry's full virtual path and its size in bytes.
        void ForEachEntry(const TFunction<void(FStringView, size_t)>& Func) const;

        // Iterates entries whose path starts with `Prefix + '/'` (or equals Prefix).
        // For a prefix like "/Game/Content", you get all assets under that subtree.
        void ForEachEntryUnder(FStringView Prefix, const TFunction<void(FStringView, size_t)>& Func) const;

    private:

        FPakArchive() = default;

        TVector<uint8>                          RawData;
        THashMap<FFixedString, FPakEntry>       Entries;
    };
}
