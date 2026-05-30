#pragma once

// Per-module DLL export macro: empty under monolithic, dllexport when building this image, else dllimport.
// Skipped entirely under REFLECTION_PARSER (API is empty there; redefining would expand undefined DLL_IMPORT and break decls).
#ifndef REFLECTION_PARSER
#ifdef LUMINA_MONOLITHIC
    #define GAMEPLAYEXTRASRUNTIME_API
#elif defined(GAMEPLAYEXTRASRUNTIME_EXPORTS)
    #define GAMEPLAYEXTRASRUNTIME_API DLL_EXPORT
#else
    #define GAMEPLAYEXTRASRUNTIME_API DLL_IMPORT
#endif
#endif
