#pragma once

#include "Archiver.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class CObject;
    struct FAssetRef;

    // Compact bit-oriented archive for replication. No FName tags or size prefixes; packs sub-byte
    // values tightly. Drive via CStruct::NetSerializeProperties / FProperty::NetSerialize.
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

        // Optional NetGUID translation for entity-ref fields. Null passes the raw value through.
        TFunction<uint32(uint32 /*LocalEntity*/)> EntityToNetGUID;
        TFunction<uint32(uint32 /*NetGUID*/)>     NetGUIDToEntity;

        // Optional object-ref index translation. When set, refs serialize as a compact varint index
        // exported once via ObjectExport. Null reads/writes the full GUID.
        TFunction<uint32(CObject*)> ObjectToNetIndex;
        TFunction<CObject*(uint32)> NetIndexToObject;

        // Same scheme for FAssetRef fields, exported once via AssetExport. Reader uses an out-param so
        // FAssetRef need not be complete here.
        TFunction<uint32(const FAssetRef&)>     AssetRefToNetIndex;
        TFunction<void(uint32, FAssetRef&)>     NetIndexToAssetRef;

    private:

        void   WriteBit(uint32 Bit);
        uint32 ReadBit();

        TVector<uint8>* WriteBytes = nullptr; // writer target (null when reading)
        const uint8*    ReadBytes  = nullptr; // reader source (null when writing)
        int64           BitCursor  = 0;
        int64           TotalBits  = 0;
    };

    // LEB128 varint over the archive byte path; 1 byte for values < 128 (the common net-index case).
    RUNTIME_API void   WriteVarUInt(FNetArchive& Ar, uint32 Value);
    RUNTIME_API uint32 ReadVarUInt(FNetArchive& Ar);
}
