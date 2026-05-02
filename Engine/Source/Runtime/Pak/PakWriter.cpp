#include "pch.h"
#include "PakWriter.h"

#include <cstring>
#include <fstream>

#include "Core/Templates/LuminaTemplate.h"
#include "Log/Log.h"

namespace Lumina
{
    bool FPakWriter::AddEntry(FStringView VirtualPath, TSpan<const uint8> Data)
    {
        FFixedString Key(VirtualPath.data(), VirtualPath.size());

        if (!SeenPaths.insert(Key).second)
        {
            // Silently de-dup; dependency walker can revisit assets via different paths.
            return false;
        }

        FPendingEntry Pending;
        Pending.VirtualPath = Move(Key);
        Pending.Data.assign(Data.begin(), Data.end());
        TotalDataSize += Pending.Data.size();
        Entries.emplace_back(Move(Pending));
        return true;
    }

    bool FPakWriter::AddEntry(FStringView VirtualPath, FStringView Data)
    {
        return AddEntry(VirtualPath, TSpan<const uint8>(reinterpret_cast<const uint8*>(Data.data()), Data.size()));
    }

    bool FPakWriter::Finalize(FStringView NativeFilePath)
    {
        std::ofstream File(FString(NativeFilePath.data(), NativeFilePath.size()).c_str(), std::ios::binary | std::ios::trunc);
        if (!File)
        {
            LOG_ERROR("FPakWriter: cannot open '{}' for write", FString(NativeFilePath.data(), NativeFilePath.size()).c_str());
            return false;
        }

        // Reserve header; patched with real TocOffset at end.
        FPakHeader Header{};
        Header.Magic      = PAK_MAGIC;
        Header.Version    = PAK_VERSION;
        Header.EntryCount = (uint32)Entries.size();
        Header.TocOffset  = 0;
        Header._Pad       = 0;

        File.write(reinterpret_cast<const char*>(&Header), sizeof(Header));

        TVector<uint64> Offsets;
        Offsets.reserve(Entries.size());

        for (const FPendingEntry& Entry : Entries)
        {
            const uint64 Offset = (uint64)File.tellp();
            Offsets.push_back(Offset);
            if (!Entry.Data.empty())
            {
                File.write(reinterpret_cast<const char*>(Entry.Data.data()), Entry.Data.size());
            }
        }

        const uint64 TocOffset = (uint64)File.tellp();
        for (size_t i = 0; i < Entries.size(); ++i)
        {
            const FPendingEntry& Entry = Entries[i];
            const uint32 PathLen = (uint32)Entry.VirtualPath.size();
            File.write(reinterpret_cast<const char*>(&PathLen), sizeof(PathLen));
            if (PathLen > 0)
            {
                File.write(Entry.VirtualPath.c_str(), PathLen);
            }

            const uint64 Offset = Offsets[i];
            const uint64 Size   = (uint64)Entry.Data.size();
            File.write(reinterpret_cast<const char*>(&Offset), sizeof(Offset));
            File.write(reinterpret_cast<const char*>(&Size),   sizeof(Size));
        }

        // Patch header with TocOffset.
        Header.TocOffset = TocOffset;
        File.seekp(0);
        File.write(reinterpret_cast<const char*>(&Header), sizeof(Header));

        if (!File.good())
        {
            LOG_ERROR("FPakWriter: write failed for '{}'", FString(NativeFilePath.data(), NativeFilePath.size()).c_str());
            return false;
        }

        LOG_INFO("FPakWriter: wrote '{}' ({} entries, {} bytes data, TOC at {})",
            FString(NativeFilePath.data(), NativeFilePath.size()).c_str(),
            (uint32)Entries.size(), TotalDataSize, TocOffset);
        return true;
    }
}
