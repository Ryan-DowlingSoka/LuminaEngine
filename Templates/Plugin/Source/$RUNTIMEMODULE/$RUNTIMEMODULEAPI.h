#pragma once

// Per-module DLL export macro. See the engine's plugin docs for the three
// build modes (monolithic Shipping / building this module / importing it).
#ifndef REFLECTION_PARSER
#ifdef LUMINA_MONOLITHIC
    #define $RUNTIMEMODULEUPPER_API
#elif defined($RUNTIMEMODULEUPPER_EXPORTS)
    #define $RUNTIMEMODULEUPPER_API DLL_EXPORT
#else
    #define $RUNTIMEMODULEUPPER_API DLL_IMPORT
#endif
#endif
