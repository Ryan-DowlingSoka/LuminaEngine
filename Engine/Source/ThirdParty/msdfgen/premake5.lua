-- msdfgen static lib; Skia/PNG/SVG compiled out (see msdfgen-config.h), only core/ + FreeType importer built.
project "msdfgen"
    kind "StaticLib"
    warnings "off"
    language "C++"
    cppdialect "C++17"

    includedirs
    {
        ".",                    -- so <msdfgen/...> self-includes resolve
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
