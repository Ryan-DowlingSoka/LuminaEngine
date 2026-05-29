#pragma once

// Per-module DLL export macro. See the engine's plugin docs for the three
// build modes (monolithic Shipping / building this module / importing it).
#ifndef REFLECTION_PARSER
#ifdef LUMINA_MONOLITHIC
    #define $EDITORMODULEUPPER_API
#elif defined($EDITORMODULEUPPER_EXPORTS)
    #define $EDITORMODULEUPPER_API DLL_EXPORT
#else
    #define $EDITORMODULEUPPER_API DLL_IMPORT
#endif
#endif
