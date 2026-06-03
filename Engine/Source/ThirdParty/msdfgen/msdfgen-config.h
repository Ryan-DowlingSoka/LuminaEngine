
#pragma once

// Hand-written replacement for the CMake-generated msdfgen-config.h. Lumina builds
// msdfgen as a static lib fed by FreeType, with no Skia / PNG / SVG support, so the
// public-symbol decoration is empty and the optional features are compiled out.
#define MSDFGEN_PUBLIC
#define MSDFGEN_EXT_PUBLIC
#define MSDFGEN_USE_CPP11
#define MSDFGEN_DISABLE_SVG
#define MSDFGEN_DISABLE_PNG
#define MSDFGEN_DISABLE_VARIABLE_FONTS
