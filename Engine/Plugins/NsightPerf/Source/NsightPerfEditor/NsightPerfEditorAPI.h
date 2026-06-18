#pragma once

// Per-module DLL export macro: empty under monolithic, dllexport when building this image, else dllimport.
#ifndef REFLECTION_PARSER
#ifdef LUMINA_MONOLITHIC
    #define NSIGHTPERFEDITOR_API
#elif defined(NSIGHTPERFEDITOR_EXPORTS)
    #define NSIGHTPERFEDITOR_API DLL_EXPORT
#else
    #define NSIGHTPERFEDITOR_API DLL_IMPORT
#endif
#endif
