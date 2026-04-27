#include "pch.h"
#include "PakArchive.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "Core/Templates/LuminaTemplate.h"
#include "Log/Log.h"

namespace Lumina
{
    namespace
    {
        // Pull T out of a byte buffer at Offset, advancing Offset.
        // Returns false if there isn't enough room — every read in the parser
        // is bounds-checked so a truncated/corrupt PAK fails cleanly instead
        // of crashing.
        template<typename T>
        bool ReadPOD(const uint8* Data, size_t Size, size_t& Offset, T& Out)
        {
            if (Offset + sizeof(T) > Size)
            {
                return false;
            }
            std::memcpy(&Out, Data + Offset, sizeof(T));
            Offset += sizeof(T);
            return true;
        }

        FString TopLevelOf(FStringView Path)
        {
            if (Path.empty() || Path[0] != '/')
            {
                return FString();
            }
            const size_t Second = Path.find('/', 1);
            return Second == FStringView::npos
                ? FString(Path.data(), Path.size())
                : FString(Path.data(), Path.data() + Second);
        }
    }

    TSharedPtr<FPakArchive> FPakArchive::Open(FStringView NativeFilePath)
    {
        // Native file IO — the PAK lives next to the cooked exe, before any
        // VFS mounts are set up.
        std::ifstream File(FString(NativeFilePath.data(), NativeFilePath.size()).c_str(), std::ios::binary | std::ios::ate);
        if (!File)
        {
            LOG_ERROR("FPakArchive: cannot open '{}'", FString(NativeFilePath.data(), NativeFilePath.size()).c_str());
            return nullptr;
        }

        const std::streamsize FileSize = File.tellg();
        if (FileSize < (std::streamsize)sizeof(FPakHeader))
        {
            LOG_ERROR("FPakArchive: '{}' is too small to be a PAK", FString(NativeFilePath.data(), NativeFilePath.size()).c_str());
            return nullptr;
        }

        TSharedPtr<FPakArchive> Archive(new FPakArchive());
        Archive->RawData.resize((size_t)FileSize);

        File.seekg(0, std::ios::beg);
        if (!File.read(reinterpret_cast<char*>(Archive->RawData.data()), FileSize))
        {
            LOG_ERROR("FPakArchive: read failed for '{}'", FString(NativeFilePath.data(), NativeFilePath.size()).c_str());
            return nullptr;
        }

        const uint8* Data = Archive->RawData.data();
        const size_t Size = Archive->RawData.size();

        FPakHeader Header{};
        size_t Cursor = 0;
        if (!ReadPOD(Data, Size, Cursor, Header))
        {
            LOG_ERROR("FPakArchive: header read failed");
            return nullptr;
        }

        if (Header.Magic != PAK_MAGIC)
        {
            LOG_ERROR("FPakArchive: bad magic 0x{:08X} (expected 0x{:08X})", Header.Magic, PAK_MAGIC);
            return nullptr;
        }
        if (Header.Version != PAK_VERSION)
        {
            LOG_ERROR("FPakArchive: version {} not supported (expected {})", Header.Version, PAK_VERSION);
            return nullptr;
        }
        if (Header.TocOffset >= Size)
        {
            LOG_ERROR("FPakArchive: TOC offset {} past end of file ({})", Header.TocOffset, Size);
            return nullptr;
        }

        // Walk the TOC entries.
        Cursor = (size_t)Header.TocOffset;
        Archive->Entries.reserve(Header.EntryCount);

        for (uint32 i = 0; i < Header.EntryCount; ++i)
        {
            uint32 PathLen = 0;
            if (!ReadPOD(Data, Size, Cursor, PathLen) || Cursor + PathLen > Size)
            {
                LOG_ERROR("FPakArchive: TOC entry {} path read failed", i);
                return nullptr;
            }

            FFixedString Path(reinterpret_cast<const char*>(Data + Cursor), PathLen);
            Cursor += PathLen;

            FPakEntry Entry{};
            if (!ReadPOD(Data, Size, Cursor, Entry.Offset) || !ReadPOD(Data, Size, Cursor, Entry.Size))
            {
                LOG_ERROR("FPakArchive: TOC entry {} offsets read failed", i);
                return nullptr;
            }

            // Sanity-check that the blob region the entry points at is actually
            // inside the file. Catches truncated PAKs early.
            if (Entry.Offset + Entry.Size > (uint64)Size || Entry.Offset >= (uint64)Size)
            {
                LOG_ERROR("FPakArchive: entry '{}' points outside the file (offset={}, size={}, file size={})",
                    Path.c_str(), Entry.Offset, Entry.Size, Size);
                return nullptr;
            }

            Archive->Entries.emplace(Move(Path), Entry);
        }

        LOG_INFO("FPakArchive: loaded '{}' ({} entries, {} bytes)",
            FString(NativeFilePath.data(), NativeFilePath.size()).c_str(),
            Header.EntryCount, Size);

        return Archive;
    }

    TSpan<const uint8> FPakArchive::ReadEntry(FStringView VirtualPath) const
    {
        FFixedString Key(VirtualPath.data(), VirtualPath.size());
        auto It = Entries.find(Key);
        if (It == Entries.end())
        {
            return {};
        }
        return TSpan<const uint8>(RawData.data() + It->second.Offset, (size_t)It->second.Size);
    }

    bool FPakArchive::HasEntry(FStringView VirtualPath) const
    {
        FFixedString Key(VirtualPath.data(), VirtualPath.size());
        return Entries.find(Key) != Entries.end();
    }

    size_t FPakArchive::EntrySize(FStringView VirtualPath) const
    {
        FFixedString Key(VirtualPath.data(), VirtualPath.size());
        auto It = Entries.find(Key);
        return It == Entries.end() ? 0 : (size_t)It->second.Size;
    }

    TVector<FString> FPakArchive::GetTopLevelAliases() const
    {
        TVector<FString> Out;
        for (const auto& [Key, Entry] : Entries)
        {
            FString Alias = TopLevelOf(FStringView(Key.data(), Key.size()));
            if (!Alias.empty() && std::find(Out.begin(), Out.end(), Alias) == Out.end())
            {
                Out.push_back(Move(Alias));
            }
        }
        return Out;
    }

    void FPakArchive::ForEachEntry(const TFunction<void(FStringView, size_t)>& Func) const
    {
        for (const auto& [Key, Entry] : Entries)
        {
            Func(FStringView(Key.data(), Key.size()), (size_t)Entry.Size);
        }
    }

    void FPakArchive::ForEachEntryUnder(FStringView Prefix, const TFunction<void(FStringView, size_t)>& Func) const
    {
        for (const auto& [Key, Entry] : Entries)
        {
            FStringView KeyView(Key.data(), Key.size());
            if (KeyView == Prefix)
            {
                continue;
            }
            if (KeyView.size() > Prefix.size()
                && KeyView.starts_with(Prefix)
                && KeyView[Prefix.size()] == '/')
            {
                Func(KeyView, (size_t)Entry.Size);
            }
        }
    }
}
