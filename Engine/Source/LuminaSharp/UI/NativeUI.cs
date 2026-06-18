using System;

namespace LuminaSharp;

/// <summary>
/// Native bindings for the World.UI scripting surface (RmlUi). Forward to the flat
/// <c>LuminaSharp_UI_*</c> exports in the Runtime module (DotNetGameplay.cpp), which delegate to the
/// locked <c>RmlUi::</c> bridge. Documents/elements/listeners cross as opaque <see cref="IntPtr"/>
/// handles; the world is its <c>CWorld*</c> as a <see cref="ulong"/>. Game thread only.
/// </summary>
public static unsafe partial class Native
{
    // Documents (loaded into the world's own screen context).
    [NativeCall] public static partial IntPtr UI_LoadDocument(ulong World, string Path);
    [NativeCall] public static partial IntPtr UI_LoadDocumentFromMemory(ulong World, string Body, string Url);
    [NativeCall] public static partial void   UI_UnloadDocument(ulong World, IntPtr Document);
    [NativeCall] public static partial void   UI_ShowDocument(IntPtr Document, int Modal, int AutoFocus);
    [NativeCall] public static partial void   UI_HideDocument(IntPtr Document);
    [NativeCall] public static partial void   UI_PullDocumentToFront(IntPtr Document);
    [NativeCall] public static partial IntPtr UI_GetDocumentRoot(IntPtr Document);

    // Element queries.
    [NativeCall] public static partial IntPtr UI_GetElementById(IntPtr Document, string Id);
    [NativeCall] public static partial IntPtr UI_QuerySelector(IntPtr Element, string Selector);

    // Element mutation.
    [NativeCall] public static partial void   UI_SetInnerRml(IntPtr Element, string Rml);
    [NativeCall] public static partial string UI_GetInnerRml(IntPtr Element);
    [NativeCall] public static partial void   UI_SetAttribute(IntPtr Element, string Name, string Value);
    [NativeCall] public static partial string UI_GetAttribute(IntPtr Element, string Name);
    [NativeCall] public static partial void   UI_SetProperty(IntPtr Element, string Name, string Value);
    [NativeCall] public static partial void   UI_RemoveProperty(IntPtr Element, string Name);
    [NativeCall] public static partial void   UI_SetClass(IntPtr Element, string Class, int Active);
    [NativeCall] public static partial int    UI_IsClassSet(IntPtr Element, string Class);
    [NativeCall] public static partial void   UI_ElementFocus(IntPtr Element);
    [NativeCall] public static partial void   UI_ElementBlur(IntPtr Element);
    [NativeCall] public static partial void   UI_ElementClick(IntPtr Element);

    // Event listeners (managed thunk + GCHandle context, like the registry signals).
    [NativeCall] public static partial IntPtr UI_AddEventListener(ulong World, IntPtr Element, string Type, IntPtr Thunk, IntPtr Context);
    [NativeCall] public static partial void   UI_RemoveEventListener(ulong World, IntPtr Listener);

    // Cursor + input routing for this world's viewport.
    [NativeCall] public static partial void   UI_SetInputMode(ulong World, int Mode);
    [NativeCall] public static partial void   UI_SetMouseMode(ulong World, int Mode);
}
