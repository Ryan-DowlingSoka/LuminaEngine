using System;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// Native -&gt; managed log severity. Mirrors the levels FDotNetHost::Export_Log switches on.
/// </summary>
public enum ELogLevel
{
    Trace = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
}

/// <summary>
/// Table of native function pointers handed to managed at bootstrap. Mirrors
/// Lumina::DotNet::FExporterTable exactly (layout + calling convention).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FExporterTable
{
    public delegate* unmanaged[Stdcall]<int, byte*, int, void> Log;
}

/// <summary>
/// Handshake payload passed to <see cref="Host.Bootstrap"/>. Mirrors Lumina::DotNet::FBootstrapArgs.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FBootstrapArgs
{
    public int AbiVersion;
    public FExporterTable* Exports;
    public IntPtr NativeModule;
}

/// <summary>
/// One script source handed from native (already read through the engine VFS). Mirrors
/// Lumina::DotNet::FSourceFile; natural alignment matches the native struct on x64.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FSourceFile
{
    public byte* Path;
    public int PathLength;
    public byte* Text;
    public int TextLength;
}
