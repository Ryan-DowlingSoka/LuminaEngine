-- RmlUi 6.0, vendored & trimmed (Core + Debugger only).
project "RmlUi"
    kind "StaticLib"
    warnings "off"
    exceptionhandling "On"

    includedirs
    {
        "Include",
        "Source/Core",
        LuminaConfig.ThirdPartyPath("FreeType/include"),
        LuminaConfig.ThirdPartyDirectory(),
    }

    -- RMLUI_STATIC_LIB must match the workspace define in BuildScripts/Dependencies.lua (else consumers see dllimport).
    defines
    {
        "RMLUI_STATIC_LIB",
        "RMLUI_VERSION=\"6.0\"",
        "RMLUI_FONT_ENGINE_FREETYPE",
    }

    links { "FreeType" }

    files
    {
        "Include/**.h",
        "Include/**.inl",
        "Source/Core/**.cpp",
        "Source/Core/**.h",
        "Source/Debugger/**.cpp",
        "Source/Debugger/**.h",
        "**.lua",
    }
