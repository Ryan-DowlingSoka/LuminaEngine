-- msdfgen 1.x (Viktor Chlumsky, MIT). Vendored as a static lib fed by FreeType, with
-- Skia / PNG / SVG support compiled out (see msdfgen-config.h). Only core/ and the
-- FreeType glyph importer (ext/import-font.cpp) are built; the editor's font baker
-- includes <msdfgen/msdfgen.h> and <msdfgen/ext/import-font.h> directly.
project "msdfgen"
    kind "StaticLib"
    warnings "off"
    language "C++"
    cppdialect "C++17"

    includedirs
    {
        ".",                    -- so <msdfgen/...> self-includes resolve via the ThirdParty root
        "..",                   -- ThirdParty root, for <msdfgen/msdfgen-config.h>
        "../FreeType/include",
    }

    files
    {
        "msdfgen.h",
        "msdfgen-ext.h",
        "msdfgen-config.h",
        "core/**.h",
        "core/**.hpp",
        "core/**.cpp",
        "ext/import-font.h",
        "ext/import-font.cpp",

        "**.lua",
    }
