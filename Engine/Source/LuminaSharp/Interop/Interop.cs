using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

// Blittable-only boundary: no runtime marshaling on any [UnmanagedCallersOnly] entry.
[assembly: DisableRuntimeMarshalling]

namespace LuminaSharp;

/// <summary>
/// Marshaling + boundary primitives shared by every native &lt;-&gt; managed crossing. This is the C#
/// analog of s&amp;box's Sandbox.Interop: nothing engine-specific lives here, just the mechanics of
/// moving UTF-8 strings across the boundary and keeping managed exceptions from unwinding into native
/// code. The engine-facing call surface is <see cref="Native"/>; the entry points are <see cref="Host"/>.
/// </summary>
internal static unsafe class Interop
{
    // A native string longer than this is treated as corrupt rather than read (guards against a bad
    // or non-terminated pointer turning into a multi-gigabyte allocation).
    private const int MaxNativeStringBytes = 64 * 1024 * 1024;

    /// <summary>Reads a counted UTF-8 buffer from native into a managed string. Null/empty -&gt; "".</summary>
    public static string GetString(byte* Pointer, int ByteLength)
    {
        if (Pointer == null || ByteLength <= 0)
        {
            return string.Empty;
        }

        return Encoding.UTF8.GetString(Pointer, ByteLength);
    }

    /// <summary>Reads a NUL-terminated UTF-8 string from native. Null -&gt; "".</summary>
    public static string GetString(byte* Pointer)
    {
        if (Pointer == null)
        {
            return string.Empty;
        }

        ReadOnlySpan<byte> Span = MemoryMarshal.CreateReadOnlySpanFromNullTerminated(Pointer);
        if (Span.Length >= MaxNativeStringBytes)
        {
            Native.Log(ELogLevel.Warn, "Interop.GetString: refusing to read an implausibly long native string.");
            return string.Empty;
        }

        return Encoding.UTF8.GetString(Span);
    }

    /// <summary>
    /// A UTF-8, NUL-terminated copy of a managed string, valid for the duration of one native call.
    /// Encodes into a caller-provided stack buffer when it fits and only heap-allocates for long
    /// strings, so the common case is allocation-free. The caller MUST <see cref="Free"/> it (a
    /// try/finally around the native call): freeing a stack-backed instance is a no-op.
    /// </summary>
    public unsafe ref struct FInteropString
    {
        /// <summary>Pointer to the UTF-8 bytes (NUL-terminated). Null when the source string was null.</summary>
        public byte* Pointer;

        /// <summary>UTF-8 byte count, excluding the NUL terminator (the length native APIs want).</summary>
        public int Length;

        private bool bHeap;

        public FInteropString(string Value, Span<byte> ScratchBuffer)
        {
            Pointer = null;
            Length = 0;
            bHeap = false;

            if (Value == null)
            {
                return;
            }

            int ByteCount = Encoding.UTF8.GetByteCount(Value);
            if (ByteCount + 1 <= ScratchBuffer.Length)
            {
                int Written = Encoding.UTF8.GetBytes(Value, ScratchBuffer);
                ScratchBuffer[Written] = 0;
                Pointer = (byte*)Unsafe.AsPointer(ref MemoryMarshal.GetReference(ScratchBuffer));
                Length = Written;
                return;
            }

            byte* Memory = (byte*)NativeMemory.Alloc((nuint)ByteCount + 1);
            fixed (char* Source = Value)
            {
                Encoding.UTF8.GetBytes(Source, Value.Length, Memory, ByteCount);
            }
            Memory[ByteCount] = 0;
            Pointer = Memory;
            Length = ByteCount;
            bHeap = true;
        }

        public void Free()
        {
            if (bHeap && Pointer != null)
            {
                NativeMemory.Free(Pointer);
                bHeap = false;
            }
            Pointer = null;
        }
    }

    /// <summary>
    /// Logs an exception caught at a native boundary. The boundary must never let a managed exception
    /// unwind into native code, so every [UnmanagedCallersOnly] entry funnels its catch here. Swallows
    /// any secondary failure from logging itself.
    /// </summary>
    public static void LogException(Exception Exception)
    {
        try
        {
            Native.Log(ELogLevel.Error, "LuminaSharp boundary exception: " + Exception);
        }
        catch
        {
            // The boundary must not throw, even if logging fails.
        }
    }
}
