#pragma once

#include "Archiver.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    // Compact, BIT-oriented archive for network replication. Unlike the disk path it writes NO FName
    // tags or per-property size prefixes, and packs sub-byte values tightly (a bool is 1 bit). Multi-byte
    // values go through a fast byte path. Drive it with CStruct::NetSerializeProperties / FProperty::NetSerialize.
    // A given instance is either a writer or a reader; FArchive's operator<< overloads work as usual.
    class RUNTIME_API FNetArchive : public FArchive
    {
    public:

        // Writer: appends bits onto OutBytes (which it does not own).
        explicit FNetArchive(TVector<uint8>& OutBytes);

        // Reader: consumes up to Size bytes from Data (which it does not own).
        FNetArchive(const uint8* Data, SIZE_T Size);

        // FArchive byte path -> whole-byte bit writes, so operator<< keeps working.
        void  Serialize(void* V, int64 Length) override;

        // Tight bit path. Reads/writes NumBits of Value (LSB-first), per the archive's mode.
        void  SerializeBits(void* Value, uint32 NumBits);
        void  SerializeBit(bool& bValue);

        int64 Tell() override      { return BitCursor / 8; }
        int64 TotalSize() override { return (TotalBits + 7) / 8; }
        void  Seek(int64 BytePos) override { BitCursor = BytePos * 8; }

        // Optional NetGUID translation for entity-reference (PROPERTY(Entity)) fields, set by the
        // replication layer. Null = pass the raw value through.
        TFunction<uint32(uint32 /*LocalEntity*/)> EntityToNetGUID;
        TFunction<uint32(uint32 /*NetGUID*/)>     NetGUIDToEntity;

    private:

        void   WriteBit(uint32 Bit);
        uint32 ReadBit();

        TVector<uint8>* WriteBytes = nullptr; // writer target (null when reading)
        const uint8*    ReadBytes  = nullptr; // reader source (null when writing)
        int64           BitCursor  = 0;
        int64           TotalBits  = 0;
    };
}
