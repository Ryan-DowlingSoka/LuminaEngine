#include "pch.h"
#include "GameplayProfiler.h"

#include "Platform/Process/PlatformProcess.h"

namespace Lumina
{
    namespace
    {
        double NowMs()
        {
            return Platform::GetTime() * 1000.0;
        }

        uint64 HashName(FStringView S)
        {
            uint64 H = 1469598103934665603ull;
            for (char C : S)
            {
                H ^= static_cast<uint8>(C);
                H *= 1099511628211ull;
            }
            return H;
        }
    }

    FGameplayProfiler& FGameplayProfiler::Get()
    {
        static FGameplayProfiler Instance;
        return Instance;
    }

    void FGameplayProfiler::SetEnabled(bool bInEnabled)
    {
        const bool bWas = bEnabled.exchange(bInEnabled, std::memory_order_relaxed);
        if (bInEnabled && !bWas)
        {
            // Fresh start so a stale partial frame can't leak in.
            Stack.clear();
            IndexOf.clear();
            Current = FGameplayProfileFrame{};
        }
    }

    void FGameplayProfiler::BeginFrame()
    {
        if (!IsEnabled())
        {
            return;
        }
        Current.Entries.clear();
        IndexOf.clear();
        Stack.clear();
        Current.TotalMs = 0.0;
        Current.FrameNumber = ++FrameCounter;
    }

    void FGameplayProfiler::EndFrame()
    {
        if (!IsEnabled())
        {
            return;
        }

        double Total = 0.0;
        for (const FGameplayProfileEntry& E : Current.Entries)
        {
            Total += E.ExclusiveMs;
        }
        Current.TotalMs = Total;
        Latest = Current;

        // Frame-total history ring (for the header graph).
        FrameTotalHistory.push_back(static_cast<float>(Total));
        if (FrameTotalHistory.size() > HistorySize)
        {
            FrameTotalHistory.erase(FrameTotalHistory.begin());
        }

        // Per-entry sparkline rings: every known scope advances this frame (0 if it didn't run), so the
        // rings stay aligned in length for a stable sparkline.
        for (auto& Pair : EntryHistory)
        {
            const auto It = IndexOf.find(Pair.first);
            const float Value = (It != IndexOf.end()) ? static_cast<float>(Current.Entries[It->second].InclusiveMs) : 0.0f;
            Pair.second.push_back(Value);
            if (Pair.second.size() > HistorySize)
            {
                Pair.second.erase(Pair.second.begin());
            }
        }
        for (const FGameplayProfileEntry& E : Current.Entries)
        {
            if (EntryHistory.find(E.Hash) == EntryHistory.end())
            {
                TVector<float> Ring;
                Ring.push_back(static_cast<float>(E.InclusiveMs));
                EntryHistory[E.Hash] = std::move(Ring);
            }
        }
    }

    void FGameplayProfiler::BeginScope(FStringView Name)
    {
        if (!IsEnabled())
        {
            return;
        }
        FOpenScope Open;
        Open.Name    = FFixedString(Name.data(), Name.size());
        Open.Hash    = HashName(Name);
        Open.StartMs = NowMs();
        Open.ChildMs = 0.0;
        Stack.push_back(std::move(Open));
    }

    void FGameplayProfiler::EndScope()
    {
        if (!IsEnabled() || Stack.empty())
        {
            return;
        }

        FOpenScope Open = std::move(Stack.back());
        Stack.pop_back();

        const double Duration = NowMs() - Open.StartMs;
        const double Self     = Duration - Open.ChildMs;

        int32 Index;
        const auto It = IndexOf.find(Open.Hash);
        if (It != IndexOf.end())
        {
            Index = It->second;
        }
        else
        {
            Index = static_cast<int32>(Current.Entries.size());
            FGameplayProfileEntry Entry;
            Entry.Name = Open.Name;
            Entry.Hash = Open.Hash;
            Current.Entries.push_back(std::move(Entry));
            IndexOf[Open.Hash] = Index;
        }

        FGameplayProfileEntry& Entry = Current.Entries[Index];
        Entry.Calls++;
        Entry.InclusiveMs += Duration;
        Entry.ExclusiveMs += Self;

        if (!Stack.empty())
        {
            Stack.back().ChildMs += Duration;
        }
    }

    const TVector<float>* FGameplayProfiler::GetEntryHistory(uint64 Hash) const
    {
        const auto It = EntryHistory.find(Hash);
        return It != EntryHistory.end() ? &It->second : nullptr;
    }

    FGameplayProfileScopeRAII::FGameplayProfileScopeRAII(FStringView Name)
    {
        bActive = FGameplayProfiler::Get().IsEnabled();
        if (bActive)
        {
            FGameplayProfiler::Get().BeginScope(Name);
        }
    }

    FGameplayProfileScopeRAII::~FGameplayProfileScopeRAII()
    {
        if (bActive)
        {
            FGameplayProfiler::Get().EndScope();
        }
    }
}
