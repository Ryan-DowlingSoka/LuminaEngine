#pragma once

#include "Platform/GenericPlatform.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "entt/entt.hpp"

namespace Lumina
{
    class FNetArchive;
    class INetworkTransport;
    struct FConnectionHandle;
    enum class ESendMode : uint8;

    // Tag: this networked entity has replicated property changes pending (set by World.Net:MarkDirty).
    // The server sends a reliable PropertyUpdate for it next tick, then clears the tag.
    struct FNetDirty {};

    namespace Net
    {
        // Reflection + entt::meta driven entity replication -- the SAME generic path the world serializer
        // uses, but keyed by a compact 32-bit type hash and carrying only Replicated properties (bit-packed
        // via FNetArchive). No per-component code: works for any REFLECT(Component).

        // Server: write one entity's replicated components: uint16 Count, then per component
        // { uint32 TypeHash, NetSerializeProperties }. Used for both Spawn and PropertyUpdate.
        void WriteEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity);

        // Client: recreate/refresh components on Entity (construct -> net-deserialize -> emplace_or_replace).
        void ReadEntityComponents(FNetArchive& Ar, entt::registry& Registry, entt::entity Entity);

        //~ Packet batching: many small messages per tick are concatenated into one datagram. Each message is
        //~ length-prefixed { uint16 ByteLen, bytes } so the receiver can split them. One ENet packet header +
        //~ one ack instead of N -- the big bandwidth/overhead win. Every packet on the wire is a batch.

        // Append one framed message to a batch buffer.
        void AppendFramedMessage(TVector<uint8>& Batch, const uint8* Msg, SIZE_T MsgSize);

        // Send a single message as a 1-message batch (for event-driven sends: RPC, peer-id handshake).
        void SendFramed(INetworkTransport& Transport, FConnectionHandle Connection, const uint8* Msg, SIZE_T MsgSize, uint8 Channel, ESendMode Mode);
        void BroadcastFramed(INetworkTransport& Transport, const uint8* Msg, SIZE_T MsgSize, uint8 Channel, ESendMode Mode);

        // Split a received batch into its framed messages.
        void ForEachFramedMessage(const uint8* Data, SIZE_T Size, const TFunction<void(const uint8*, SIZE_T)>& Fn);
    }
}
