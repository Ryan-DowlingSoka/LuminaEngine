#include "CookDDC.h"

#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"

#include <atomic>
#include <cstdio>
#include <filesystem>


namespace Lumina
{
    namespace
    {
        std::atomic<size_t> GHits{0};
        std::atomic<size_t> GMisses{0};
        std::atomic<size_t> GWrittenBytes{0};

        FString DDCBaseDir()
        {
            FString Out = Paths::GetEngineInstallDirectory();
            if (Out.empty()) return {};
            Out += "/Intermediates/DDC";
            return Out;
        }

        FString DDCPathFor(const FCookInputHash& Key)
        {
            char Hex[17];
            std::snprintf(Hex, sizeof(Hex), "%016llx",
                static_cast<unsigned long long>(Key.Hash));

            FString Out = DDCBaseDir();
            if (Out.empty()) return {};
            Out += '/';
            Out += Hex[0];
            Out += Hex[1];
            Out += '/';
            Out += Hex;
            Out += ".ddc";
            return Out;
        }
    }


    FCookInputHash FCookDDC::ComputeKey(uint64 SourceContentHash)
    {
        if (SourceContentHash == 0)
        {
            return {};
        }
        // Mix the cook stamp in with a large odd multiplier so stamp bumps
        // shuffle every key off its prior bucket. No claim of cryptographic
        // strength — collision risk is dominated by the source hash itself.
        static constexpr uint64 kMixer = 0x9E3779B97F4A7C15ull;
        const uint64 H = SourceContentHash ^ (static_cast<uint64>(kCookStamp) * kMixer);
        return { H == 0 ? 1ull : H };
    }

    bool FCookDDC::TryGet(const FCookInputHash& Key, TVector<uint8>& OutBytes)
    {
        if (!Key.IsValid())
        {
            return false;
        }

        const FString Path = DDCPathFor(Key);
        if (Path.empty())
        {
            return false;
        }

        std::error_code Ec;
        if (!std::filesystem::exists(Path.c_str(), Ec) || Ec)
        {
            GMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (!FileHelper::LoadFileToArray(OutBytes, Path))
        {
            GMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        GHits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool FCookDDC::Put(const FCookInputHash& Key, const TVector<uint8>& Bytes)
    {
        if (!Key.IsValid() || Bytes.empty())
        {
            return false;
        }

        const FString Path = DDCPathFor(Key);
        if (Path.empty())
        {
            return false;
        }

        std::error_code Ec;
        std::filesystem::create_directories(
            std::filesystem::path(Path.c_str()).parent_path(), Ec);
        if (Ec)
        {
            return false;
        }

        if (!FileHelper::SaveArrayToFile(Bytes, Path))
        {
            return false;
        }

        GWrittenBytes.fetch_add(Bytes.size(), std::memory_order_relaxed);
        return true;
    }

    void   FCookDDC::Reset()        { GHits = 0; GMisses = 0; GWrittenBytes = 0; }
    size_t FCookDDC::Hits()         { return GHits.load(std::memory_order_relaxed); }
    size_t FCookDDC::Misses()       { return GMisses.load(std::memory_order_relaxed); }
    size_t FCookDDC::WrittenBytes() { return GWrittenBytes.load(std::memory_order_relaxed); }
}
