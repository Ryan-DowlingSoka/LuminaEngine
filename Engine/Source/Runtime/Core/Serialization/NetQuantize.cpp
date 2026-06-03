#include "pch.h"
#include "NetQuantize.h"
#include "NetArchive.h"
#include <cmath>

namespace Lumina::NetQuantize
{
    namespace
    {
        // --- bit-stream primitives (LSB-first via FNetArchive::SerializeBits) ---

        void PutBits(FNetArchive& Ar, uint64 Value, uint32 NumBits) { Ar.SerializeBits(&Value, NumBits); }

        uint64 GetBits(FNetArchive& Ar, uint32 NumBits)
        {
            uint64 Value = 0;
            Ar.SerializeBits(&Value, NumBits);
            return Value;
        }

        // LEB128 varint (7 data bits + continuation bit per byte).
        void WriteVarUInt64(FNetArchive& Ar, uint64 Value)
        {
            do
            {
                uint8 Byte = static_cast<uint8>(Value & 0x7F);
                Value >>= 7;
                if (Value != 0) { Byte |= 0x80; }
                Ar.SerializeBits(&Byte, 8);
            }
            while (Value != 0);
        }

        uint64 ReadVarUInt64(FNetArchive& Ar)
        {
            uint64 Result = 0;
            uint32 Shift  = 0;
            uint8  Byte   = 0;
            do
            {
                Byte = 0;
                Ar.SerializeBits(&Byte, 8);
                Result |= static_cast<uint64>(Byte & 0x7F) << Shift;
                Shift += 7;
            }
            while ((Byte & 0x80) != 0 && Shift < 64);
            return Result;
        }

        // Map signed <-> unsigned so small-magnitude values varint to few bytes.
        uint64 ZigZag(int64 V)   { return (static_cast<uint64>(V) << 1) ^ static_cast<uint64>(V >> 63); }
        int64  UnZigZag(uint64 V){ return static_cast<int64>(V >> 1) ^ -static_cast<int64>(V & 1); }

        int64 RoundToInt64(double D) { return static_cast<int64>(D < 0.0 ? D - 0.5 : D + 0.5); }

        // --- quaternion smallest-three constants ---
        constexpr uint32 QuatComponentBits = 15;
        constexpr uint32 QuatMaxQ          = (1u << QuatComponentBits) - 1;
        constexpr float  QuatRange         = 0.70710678118654752440f; // 1/sqrt(2): bound on the 3 smallest components
    }

    void WritePackedVector(FNetArchive& Ar, const FVector3& V)
    {
        // Quantize in double so large world coords keep mm precision before the cast.
        WriteVarUInt64(Ar, ZigZag(RoundToInt64(static_cast<double>(V.x) / PositionQuantum)));
        WriteVarUInt64(Ar, ZigZag(RoundToInt64(static_cast<double>(V.y) / PositionQuantum)));
        WriteVarUInt64(Ar, ZigZag(RoundToInt64(static_cast<double>(V.z) / PositionQuantum)));
    }

    void ReadPackedVector(FNetArchive& Ar, FVector3& V)
    {
        V.x = static_cast<float>(UnZigZag(ReadVarUInt64(Ar)) * PositionQuantum);
        V.y = static_cast<float>(UnZigZag(ReadVarUInt64(Ar)) * PositionQuantum);
        V.z = static_cast<float>(UnZigZag(ReadVarUInt64(Ar)) * PositionQuantum);
    }

    void WritePackedQuat(FNetArchive& Ar, const FQuat& Q)
    {
        const float C[4] = { Q.x, Q.y, Q.z, Q.w };

        // Find the largest-magnitude component; it's the one we drop and reconstruct.
        int   Largest = 0;
        float LargestAbs = std::fabs(C[0]);
        for (int i = 1; i < 4; ++i)
        {
            const float A = std::fabs(C[i]);
            if (A > LargestAbs) { LargestAbs = A; Largest = i; }
        }

        // q and -q are the same rotation; choose the sign that makes the dropped component positive,
        // so the reader can reconstruct it as +sqrt(...) with no sign bit.
        const float Sign = (C[Largest] < 0.0f) ? -1.0f : 1.0f;

        PutBits(Ar, static_cast<uint64>(Largest), 2);
        for (int i = 0; i < 4; ++i)
        {
            if (i == Largest) { continue; }
            float A = C[i] * Sign;                       // in [-QuatRange, QuatRange]
            float Norm = (A / QuatRange + 1.0f) * 0.5f;  // -> [0, 1]
            Norm = Norm < 0.0f ? 0.0f : (Norm > 1.0f ? 1.0f : Norm);
            PutBits(Ar, static_cast<uint64>(Norm * QuatMaxQ + 0.5f), QuatComponentBits);
        }
    }

    void ReadPackedQuat(FNetArchive& Ar, FQuat& Q)
    {
        const int Largest = static_cast<int>(GetBits(Ar, 2));

        float C[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float SumSq = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (i == Largest) { continue; }
            const uint32 Quantized = static_cast<uint32>(GetBits(Ar, QuatComponentBits));
            const float  Norm = static_cast<float>(Quantized) / static_cast<float>(QuatMaxQ);
            const float  A    = (Norm * 2.0f - 1.0f) * QuatRange;
            C[i]   = A;
            SumSq += A * A;
        }
        C[Largest] = std::sqrt(SumSq < 1.0f ? 1.0f - SumSq : 0.0f);

        // Renormalize: quantization error can push the magnitude slightly off unit.
        const float LenSq = C[0] * C[0] + C[1] * C[1] + C[2] * C[2] + C[3] * C[3];
        const float Inv   = (LenSq > 1e-12f) ? 1.0f / std::sqrt(LenSq) : 1.0f;
        Q.x = C[0] * Inv;
        Q.y = C[1] * Inv;
        Q.z = C[2] * Inv;
        Q.w = C[3] * Inv;
    }
}
