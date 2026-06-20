using System;
using System.Runtime.CompilerServices;
using System.Text;

namespace LuminaSharp;

// Zero-crossing readers over native EASTL FString / TVector laid out in place (little-endian; layout guarded
// by VerifyEASTLInteropLayout). Returned views alias native storage: read synchronously, never retain.
public static unsafe class NativeMarshal
{
    // eastl::basic_string<char>: 24-byte SSO/heap union, last byte = flag (top bit set => heap, ptr@0 size@8).
    private const int FStringFlagOffset = 23;
    private const int FStringSSOCapacity = 23;

    // Longer than this => treated as a corrupt read, not a multi-gigabyte allocation.
    private const long MaxNativeStringBytes = 64 * 1024 * 1024;

    /// <summary>Transcodes a native FString to a managed string in place (no crossing); decodes the SSO/heap union.</summary>
    public static string ReadString(nint FStringPtr)
    {
        if (FStringPtr == 0)
        {
            return string.Empty;
        }

        byte* Base = (byte*)FStringPtr;
        byte Flag = Base[FStringFlagOffset];
        byte* Data;
        long Length;
        if ((Flag & 0x80) != 0)
        {
            Data = *(byte**)Base;                   // heap.mpBegin
            Length = (long)*(ulong*)(Base + 8);     // heap.mnSize
        }
        else
        {
            Data = Base;                            // sso.mData (stored in place)
            Length = FStringSSOCapacity - Flag;     // SSO_CAPACITY - remainingSize
        }

        if (Data == null || Length <= 0 || Length >= MaxNativeStringBytes)
        {
            return string.Empty;
        }

        return Encoding.UTF8.GetString(Data, (int)Length);
    }

    /// <summary>A zero-copy span over a native TVector&lt;T&gt; embedded at Container+Offset (blittable element).</summary>
    public static ReadOnlySpan<T> ReadVector<T>(nint Container, nint Offset) where T : unmanaged
    {
        return DecodeVector<T>((byte*)Container + Offset);
    }

    // The single source of truth for the EASTL vector layout (mpBegin@0, mpEnd@8); shared by ReadVector and NativeList.
    internal static Span<T> DecodeVector<T>(byte* Header) where T : unmanaged
    {
        byte* Begin = *(byte**)Header;
        byte* End = *(byte**)(Header + sizeof(void*));
        if (Begin == null || End <= Begin)
        {
            return Span<T>.Empty;
        }
        return new Span<T>(Begin, (int)((End - Begin) / Unsafe.SizeOf<T>()));
    }
}
