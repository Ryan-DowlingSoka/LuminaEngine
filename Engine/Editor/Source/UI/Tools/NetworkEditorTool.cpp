#include "NetworkEditorTool.h"

#include "World/WorldManager.h"
#include "World/WorldContext.h"
#include "World/World.h"
#include "World/Net/NetWorldState.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "Networking/INetworkTransport.h"

namespace Lumina
{
    namespace
    {
        const char* NetModeName(ENetMode Mode)
        {
            switch (Mode)
            {
            case ENetMode::Standalone:      return "Standalone";
            case ENetMode::Client:          return "Client";
            case ENetMode::ListenServer:    return "Listen Server";
            case ENetMode::DedicatedServer: return "Dedicated Server";
            }
            return "Unknown";
        }

        const char* ConnStateName(EConnectionState State)
        {
            switch (State)
            {
            case EConnectionState::Disconnected: return "Disconnected";
            case EConnectionState::Connecting:   return "Connecting";
            case EConnectionState::Connected:    return "Connected";
            }
            return "?";
        }

        FString FormatBytesNice(double Bytes)
        {
            const char* Units[] = { "B", "KB", "MB", "GB" };
            int Unit = 0;
            while (Bytes >= 1024.0 && Unit < 3) { Bytes /= 1024.0; ++Unit; }
            char Buf[64];
            snprintf(Buf, sizeof(Buf), "%.2f %s", Bytes, Units[Unit]);
            return FString(Buf);
        }

        // A networked world is one whose registry has a live FNetWorldState with a transport.
        FNetWorldState* GetNetState(CWorld* World)
        {
            if (World == nullptr) { return nullptr; }
            FNetWorldState* State = World->GetEntityRegistry().ctx().find<FNetWorldState>();
            return (State != nullptr && State->Transport) ? State : nullptr;
        }
    }

    void FNetworkEditorTool::OnInitialize()
    {
        CreateToolWindow("Network", [&](bool bIsFocused)
        {
            DrawNetworkWindow(bIsFocused);
        });
    }

    void FNetworkEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        Histories.clear();
    }

    void FNetworkEditorTool::SampleRates(CWorld* World, FNetHistory& History, float DeltaSeconds)
    {
        FNetWorldState* State = GetNetState(World);
        if (State == nullptr) { return; }

        const FNetworkStats Stats = State->Transport->GetStats();
        if (History.bPrimed && DeltaSeconds > 0.0f)
        {
            const float SendRate = static_cast<float>(Stats.TotalSentBytes     - History.LastSent) / DeltaSeconds;
            const float RecvRate = static_cast<float>(Stats.TotalReceivedBytes - History.LastRecv) / DeltaSeconds;
            History.SendRate[History.Head] = SendRate;
            History.RecvRate[History.Head] = RecvRate;
            History.Head = (History.Head + 1) % FNetHistory::Capacity;
        }
        History.LastSent = Stats.TotalSentBytes;
        History.LastRecv = Stats.TotalReceivedBytes;
        History.bPrimed  = true;
    }

    void FNetworkEditorTool::DrawNetworkWindow(bool bIsFocused)
    {
        if (GWorldManager == nullptr)
        {
            ImGui::TextDisabled("World manager not initialized.");
            return;
        }

        // Gather networked worlds, and keep their rate history sampled every frame (even when unselected).
        const float Dt = ImGui::GetIO().DeltaTime;
        TVector<CWorld*> NetWorlds;
        for (const TUniquePtr<FWorldContext>& Ctx : GWorldManager->GetContexts())
        {
            CWorld* World = Ctx->World.Get();
            if (GetNetState(World) == nullptr)
            {
                continue;
            }
            NetWorlds.push_back(World);
            SampleRates(World, Histories[World], Dt);
        }

        if (NetWorlds.empty())
        {
            ImGui::TextDisabled("No active network sessions.");
            ImGui::TextWrapped("Start a multiplayer PIE session (Play settings -> Net Mode) to see connections and stats here.");
            SelectedWorld = nullptr;
            return;
        }

        // Default / validate the selection.
        bool bSelectedValid = false;
        for (CWorld* W : NetWorlds) { if (W == SelectedWorld) { bSelectedValid = true; break; } }
        if (!bSelectedValid) { SelectedWorld = NetWorlds[0]; }

        // --- World selector ---
        auto WorldLabel = [](CWorld* W) -> FString
        {
            char Buf[128];
            snprintf(Buf, sizeof(Buf), "[%s] %s", NetModeName(W->GetNetMode()), W->GetName().c_str());
            return FString(Buf);
        };

        ImGui::TextUnformatted("Session:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::BeginCombo("##NetWorld", WorldLabel(SelectedWorld).c_str()))
        {
            for (CWorld* W : NetWorlds)
            {
                const bool bSelected = (W == SelectedWorld);
                if (ImGui::Selectable(WorldLabel(W).c_str(), bSelected))
                {
                    SelectedWorld = W;
                }
                if (bSelected) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d networked world%s)", (int)NetWorlds.size(), NetWorlds.size() == 1 ? "" : "s");

        ImGui::Separator();

        FNetWorldState* State = GetNetState(SelectedWorld);
        if (State == nullptr)
        {
            ImGui::TextDisabled("Selected world is no longer networked.");
            return;
        }

        FEntityRegistry& Registry = SelectedWorld->GetEntityRegistry();
        const bool bIsServer = SelectedWorld->GetNetMode() == ENetMode::ListenServer
                            || SelectedWorld->GetNetMode() == ENetMode::DedicatedServer;

        // --- Overview ---
        if (ImGui::CollapsingHeader("Overview", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int NetworkedEntities = 0;
            for (auto E : Registry.view<SNetworkComponent>()) { (void)E; ++NetworkedEntities; }

            ImGui::BulletText("Role: %s", NetModeName(SelectedWorld->GetNetMode()));
            ImGui::BulletText("Local Peer Id: %u%s", State->LocalPeerId, bIsServer ? " (server)" : "");
            if (bIsServer)
            {
                ImGui::BulletText("Connected Clients: %d", State->ConnectedClients);
            }
            else
            {
                ImGui::BulletText("Connected to server: %s", State->bClientConnected ? "yes" : "no");
            }
            ImGui::BulletText("Networked Entities: %d", NetworkedEntities);
            ImGui::BulletText("NetGUID Table: %d entries", (int)State->GuidTable.GuidToEntity.size());
            ImGui::BulletText("Dynamic Spawns Tracked: %d", (int)State->KnownSpawnedGuids.size());
        }

        // --- Transport totals + throughput ---
        const FNetworkStats Stats = State->Transport->GetStats();
        if (ImGui::CollapsingHeader("Transport", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("##Totals", 2, ImGuiTableFlags_SizingFixedFit))
            {
                auto Row = [](const char* K, const FString& V)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(K);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(V.c_str());
                };
                Row("Sent",            FormatBytesNice((double)Stats.TotalSentBytes));
                Row("Received",        FormatBytesNice((double)Stats.TotalReceivedBytes));
                Row("Packets Sent",    FString(eastl::to_string(Stats.TotalSentPackets).c_str()));
                Row("Packets Recv",    FString(eastl::to_string(Stats.TotalReceivedPackets).c_str()));
                ImGui::EndTable();
            }

            // Throughput graph (bytes/sec), ring buffer plotted from Head.
            FNetHistory& H = Histories[SelectedWorld];
            const float CurSend = H.SendRate[(H.Head + FNetHistory::Capacity - 1) % FNetHistory::Capacity];
            const float CurRecv = H.RecvRate[(H.Head + FNetHistory::Capacity - 1) % FNetHistory::Capacity];

            ImGui::Spacing();
            ImGui::Text("Send: %s/s", FormatBytesNice(CurSend).c_str());
            ImGui::PlotLines("##Send", H.SendRate, FNetHistory::Capacity, H.Head, nullptr, 0.0f, FLT_MAX, ImVec2(-1, 60));
            ImGui::Text("Recv: %s/s", FormatBytesNice(CurRecv).c_str());
            ImGui::PlotLines("##Recv", H.RecvRate, FNetHistory::Capacity, H.Head, nullptr, 0.0f, FLT_MAX, ImVec2(-1, 60));
        }

        // --- Per-connection stats ---
        TVector<FConnectionStats> Conns;
        State->Transport->GetConnectionStats(Conns);
        if (ImGui::CollapsingHeader("Connections", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (Conns.empty())
            {
                ImGui::TextDisabled("No peers.");
            }
            else if (ImGui::BeginTable("##Conns", 8,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Id");
                ImGui::TableSetupColumn("State");
                ImGui::TableSetupColumn("RTT");
                ImGui::TableSetupColumn("Loss");
                ImGui::TableSetupColumn("Sent");
                ImGui::TableSetupColumn("Recv");
                ImGui::TableSetupColumn("Pkts");
                ImGui::TableSetupColumn("Lost");
                ImGui::TableHeadersRow();

                for (const FConnectionStats& C : Conns)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%u", C.ConnectionId);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(ConnStateName(C.State));
                    ImGui::TableNextColumn(); ImGui::Text("%u ms", C.RoundTripTimeMs);
                    ImGui::TableNextColumn(); ImGui::Text("%.1f%%", C.PacketLoss * 100.0f);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytesNice((double)C.SentBytes).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytesNice((double)C.ReceivedBytes).c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%u", C.PacketsSent);
                    ImGui::TableNextColumn(); ImGui::Text("%u", C.PacketsLost);
                }
                ImGui::EndTable();
            }
        }
    }

    void FNetworkEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Sessions",
            "Each networked world (the listen server and every client preview) is a separate session. "
            "Pick one to inspect its transport and connections.");
        DrawHelpTextRow("Transport",
            "Cumulative bytes/packets the ENet host has sent/received, plus a rolling bytes/sec graph.");
        DrawHelpTextRow("Connections",
            "Per-peer round-trip time, measured packet loss, and throughput. RTT/loss populate after a few "
            "reliable round-trips. Use the Play settings' Network Simulation to inject latency/loss.");
    }
}
