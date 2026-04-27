-- RmlUi 6.0, vendored & trimmed.
-- Kept: Source/Core (incl. FontEngineDefault), Source/Debugger, public
-- headers under Include/{Core,Config,Debugger}. Stripped: samples, tests,
-- backends, Lottie/SVG/Lua plugins, CMake.
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

    -- RMLUI_STATIC_LIB is also workspace-defined in BuildScripts/Dependencies.lua
    -- so consumer headers see no dllimport. Re-listing here for the lib itself.
    defines
    {
        "RMLUI_STATIC_LIB",
        "RMLUI_VERSION=\"6.0\"",
        "RMLUI_FONT_ENGINE_FREETYPE",
    }

    -- Final symbols resolve in Runtime.dll; listed here for explicit dep.
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
