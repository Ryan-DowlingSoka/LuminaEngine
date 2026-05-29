#pragma once

// Per-module DLL export macro. Pattern handles three build modes:
//  - LUMINA_MONOLITHIC (Shipping): all modules statically linked into the
//    final exe; export/import is meaningless and would produce a linker
//    warning. Define empty.
//  - <NAME>_EXPORTS set:  this module's own image is being built (premake's
//    `{prj.name:upper()}_EXPORTS` define from Workspace.lua). dllexport.
//  - otherwise: another image is including this header to reference our
//    symbols. dllimport.
// Reflection parser runs without DLL_IMPORT/DLL_EXPORT defined and gets the
// module API as an empty macro via the Reflector's command-line `-D<NAME>_API=`.
// Redefining it here would expand to the undefined `DLL_IMPORT` token and break
// any subsequent struct/class decl in this module, so skip the dance entirely.
#ifndef REFLECTION_PARSER
#ifdef LUMINA_MONOLITHIC
    #define GAMEPLAYEXTRASRUNTIME_API
#elif defined(GAMEPLAYEXTRASRUNTIME_EXPORTS)
    #define GAMEPLAYEXTRASRUNTIME_API DLL_EXPORT
#else
    #define GAMEPLAYEXTRASRUNTIME_API DLL_IMPORT
#endif
#endif
