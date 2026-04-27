-- FreeType 2.13.3, trimmed to TrueType + OpenType-CFF for RmlUi's
-- FontEngineDefault. See include/freetype/config/ftmodule.h and ftoption.h
-- for the matching vendor edits. Re-adding a module means restoring its
-- src/ subdir, listing the entry .c below, and updating ftmodule.h.
project "FreeType"
    kind "StaticLib"
    warnings "off"
    language "C"

    includedirs
    {
        "include",
        "src",
    }

    -- FT2_BUILD_LIBRARY: building FreeType itself. Consumers only see the
    -- public <freetype/...> headers via the include path in Dependencies.lua.
    defines
    {
        "FT2_BUILD_LIBRARY",
        "_CRT_SECURE_NO_WARNINGS",
        "_CRT_NONSTDC_NO_WARNINGS",
    }

    files
    {
        "include/freetype/**.h",

        "src/autofit/autofit.c",

        -- Base library.
        "src/base/ftsystem.c",
        "src/base/ftinit.c",
        "src/base/ftdebug.c",
        "src/base/ftbase.c",
        "src/base/ftbbox.c",
        "src/base/ftbitmap.c",
        "src/base/ftgasp.c",
        "src/base/ftglyph.c",
        "src/base/ftmm.c",
        "src/base/ftpatent.c",
        "src/base/ftstroke.c",
        "src/base/ftsynth.c",
        "src/base/fttype1.c",
        "src/base/ftbdf.c",
        "src/base/ftcid.c",
        "src/base/ftfstype.c",
        "src/base/ftgxval.c",
        "src/base/ftotval.c",
        "src/base/ftpfr.c",
        "src/base/ftwinfnt.c",

        -- Format module entries (each .c amalgamates its directory).
        "src/cff/cff.c",
        "src/sfnt/sfnt.c",
        "src/truetype/truetype.c",

        -- PostScript helpers (CFF / OpenType .otf).
        "src/psaux/psaux.c",
        "src/pshinter/pshinter.c",
        "src/psnames/psnames.c",

        "src/raster/raster.c",
        "src/smooth/smooth.c",

        "**.lua",
    }
