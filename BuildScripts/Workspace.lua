--[[
    Lumina Workspace Settings

    The single canonical set of workspace-scoped build settings: configurations,
    platforms, preprocessor defines, warning suppressions and per-config/per-
    system filters. Both the engine workspace (root premake5.lua) and external
    game workspaces (BuildScripts/GameProject) call LuminaWorkspaceSettings()
    so engine headers and third-party headers compile with IDENTICAL
    preprocessor state everywhere. Anything that affects ABI (e.g. the LUA_*TAG
    limits, RMLUI_STATIC_LIB, NOMINMAX) must live here and nowhere else.

        LuminaWorkspaceSettings({
            Name         = "Lumina",
            StartProject = "Lumina",
            TargetDir    = LuminaConfig.GetTargetDirectory(),
            ObjDir       = LuminaConfig.GetObjDirectory(),
        })
]]

assert(LuminaConfig, "Workspace.lua must be included after BuildScripts/Dependencies")

function LuminaWorkspaceSettings(Opts)
    assert(Opts and Opts.Name, "LuminaWorkspaceSettings: Name is required")

    workspace(Opts.Name)
        language "C++"
        cppdialect "C++latest"
        architecture "x86_64"
        warnings "Default"
        targetdir (Opts.TargetDir or LuminaConfig.GetTargetDirectory())
        objdir (Opts.ObjDir or LuminaConfig.GetObjDirectory())
        enableunitybuild "Off"
        fastuptodate "On"
        multiprocessorcompile "On"
        vectorextensions "AVX2"

        if Opts.StartProject then
            startproject(Opts.StartProject)
        end

        configurations { "Debug", "Development", "Shipping" }
        platforms { "Editor", "Game" }
        defaultplatform "Editor"

        filter "configurations:Debug"
            defines
            {
                "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
                "JPH_EXTERNAL_PROFILE",
                "JPH_ENABLE_ASSERTS",
                "LUAI_GCMETRICS",
            }
        filter {}

        defines
        {
            "JPH_OBJECT_LAYER_BITS=32",
            "JPH_DEBUG_RENDERER",
            "EASTL_USER_DEFINED_ALLOCATOR=1",
            "_CRT_SECURE_NO_WARNINGS",
            "_SILENCE_CXX23_ALIGNED_UNION_DEPRECATION_WARNING",
            "_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
            "GLM_FORCE_DEPTH_ZERO_TO_ONE",
            "GLM_FORCE_LEFT_HANDED",
            "GLM_ENABLE_EXPERIMENTAL",
            "GLM_FORCE_AVX2",
            "IMGUI_DEFINE_MATH_OPERATORS",
            "IMGUI_IMPL_VULKAN_USE_VOLK",
            "WITH_AFTERMATH",

            "RMLUI_STATIC_LIB",

            -- ABI-critical: these size Luau's user/light-userdata tag tables.
            -- Engine and game MUST agree or script types mismatch at the DLL
            -- boundary. Keep them here, not duplicated per workspace.
            "LUA_VECTOR_SIZE=4",
            "LUA_UTAG_LIMIT=2000",
            "LUA_LUTAG_LIMIT=2000",

            "TRACY_ALLOW_SHADOW_WARNING",
            "TRACY_ENABLE",
            "TRACY_CALLSTACK",
            "TRACY_ON_DEMAND",

            'LUMINA_SYSTEM_NAME=\"%{LuminaConfig.GetSystem()}\"',
            'LUMINA_ARCH_NAME=\"%{LuminaConfig.GetArchitecture()}\"',
            'LUMINA_CONFIGURATION_NAME=\"%{cfg.buildcfg}\"',
            'LUMINA_PLATFORM_NAME=\"%{LuminaConfig.GetSystem()}%{LuminaConfig.GetArchitecture()}\"',
            'LUMINA_SHAREDLIB_EXT_NAME=\"%{LuminaConfig.GetSharedLibExtName()}\"',
        }

        disablewarnings
        {
            "4251", -- DLL-interface warning
            "4275", -- Non-DLL-interface base class
            "4244", -- Precision loss warnings
            "4267", -- Precision loss warnings
        }

        filter "kind:SharedLib"
            defines { "%{prj.name:upper()}_EXPORTS" }
        filter {}

        filter "architecture:64"
            defines { "LUMINA_PLATFORM_CPU_X86_64" }

        filter "system:windows"
            systemversion "latest"
            conformancemode "On"
            defines
            {
                "LE_PLATFORM_WINDOWS",
                "DLL_EXPORT=__declspec(dllexport)",
                "DLL_IMPORT=__declspec(dllimport)",
                "__AVX2__",
                -- Block <windows.h> from defining min/max macros, which clobber
                -- std::max/glm::min etc.
                "NOMINMAX",
                "WIN32_LEAN_AND_MEAN",
            }
            buildoptions
            {
                "/arch:AVX2",
                "/Zc:preprocessor",
                "/Zc:inline",
                "/Zc:__cplusplus",
                "/bigobj",
            }
        filter {}

        filter "platforms:Game"
            defines { "WITH_EDITOR=0" }
        filter "platforms:Editor"
            defines { "WITH_EDITOR=1" }
        filter {}

        filter "configurations:Debug"
            targetsuffix "-Debug"
            linktimeoptimization "Off"
            incrementallink "On"
            optimize "Off"
            symbols "On"
            -- /Z7 embeds debug info in the .obj, avoiding mspdbsrv serialization
            -- during /MP compile. Pairs with /DEBUG:FASTLINK so the linker just
            -- references the .objs instead of merging everything into a PDB.
            debugformat "c7"
            editandcontinue "Off"
            runtime "Debug"
            defines { "LE_DEBUG", "LUMINA_DEBUG", "_DEBUG", "DEBUG" }

        -- FASTLINK is linker-only; scoping to non-StaticLib avoids LNK4044 spam
        -- on third-party static libs (lib.exe ignores it).
        filter { "configurations:Debug", "kind:not StaticLib" }
            linkoptions { "/DEBUG:FASTLINK" }

        filter "configurations:Development"
            targetsuffix "-Development"
            optimize "Speed"
            symbols "On"
            runtime "Release"
            -- LTO turns every relink into a whole-program codegen pass; engine
            -- programmers iterate in Development, so keep incremental link.
            linktimeoptimization "Off"
            incrementallink "On"
            debugformat "c7"
            editandcontinue "Off"
            defines { "NDEBUG", "LE_DEVELOPMENT", "LUMINA_DEVELOPMENT" }

        filter { "configurations:Development", "kind:not StaticLib" }
            linkoptions { "/OPT:NOICF", "/OPT:NOREF", "/DEBUG:FASTLINK" }

        filter "configurations:Shipping"
            targetsuffix "-Shipping"
            linktimeoptimization "On"
            optimize "Full"
            symbols "Off"
            runtime "Release"
            defines { "NDEBUG", "LE_SHIPPING", "LUMINA_SHIPPING" }
            removedefines { "TRACY_ENABLE", "JPH_DEBUG_RENDERER" }

        filter {}
end
