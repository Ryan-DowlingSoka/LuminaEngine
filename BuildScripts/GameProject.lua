--[[
    Lumina Game Project Build System

    Provides LuminaGameProject() for declaring an EXTERNAL game project that
    links against a pre-built Lumina engine install. Unlike LuminaModule()
    (which is used for engine-internal modules), this function creates the
    entire premake workspace in addition to the project, since external
    game projects have their own .sln separate from the engine's.

    Usage (in an external project's premake5.lua):

        local LuminaDir = os.getenv("LUMINA_DIR")
        include(path.join(LuminaDir, "BuildScripts/Dependencies"))
        include(path.join(LuminaDir, "BuildScripts/GameProject"))

        LuminaGameProject({
            Name = "MyGame",
            Dependencies = { "ImGui", "EA", "Tracy", "RPMalloc", "EnkiTS", "Luau" },
        })

    Required:
        Name            - Project / workspace name

    Optional:
        Dependencies            - Third-party libs to link (defaults to the
                                  standard game set). These are linked from
                                  the engine's Binaries directory.
        ModuleDependencies      - Additional engine modules to link (Runtime
                                  is always linked).
        EditorModuleDependencies- Engine modules to link only on Editor
                                  platform (Editor is always added).
        Reflection              - Enable reflection generation (default true).
        PrivateDefines          - Extra defines for this project only.
        ExtraFiles              - Extra file patterns to include in the project.
        ExtraLibDirs            - Extra library search paths.
        ExtraLinks              - Extra raw library names to link.
]]

-- Standard third-party dependencies every Lumina game project needs.
-- These mirror what the Sandbox project uses and represent the minimum
-- set required for engine headers to link cleanly in game code.
local DefaultGameDependencies =
{
    "ImGui",
    "RPMalloc",
    "EA",
    "EnkiTS",
    "Tracy",
    "Luau",
}


local function SetupWorkspace(Def)
    workspace(Def.Name)
        language "C++"
        cppdialect "C++latest"
        architecture "x86_64"
        warnings "Default"
        -- Game project outputs into its own Binaries folder. The engine
        -- loads the game DLL from {ProjectPath}/Binaries/{Platform}/...
        targetdir (path.join("%{wks.location}", "Binaries", LuminaConfig.OutputDirectory))
        objdir (path.join("%{wks.location}", "Intermediates", "Obj", LuminaConfig.OutputDirectory, "%{prj.name}"))
        enableunitybuild "Off"
        fastuptodate "On"
        multiprocessorcompile "On"
        startproject(Def.Name)
        vectorextensions "AVX2"

        configurations { "Debug", "Development", "Shipping" }
        platforms { "Editor", "Game" }
        defaultplatform "Editor"

        -- Mirror the engine workspace defines exactly so engine headers and
        -- third-party headers compile with identical preprocessor state.
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
            "GLM_FORCE_INTRINSICS",
            "IMGUI_DEFINE_MATH_OPERATORS",
            "IMGUI_IMPL_VULKAN_USE_VOLK",

            "LUA_VECTOR_SIZE=4",
            "LUA_UTAG_LIMIT=1000",
            "LUA_LUTAG_LIMIT=1000",

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

        disablewarnings { "4251", "4275", "4244", "4267" }

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
            runtime "Debug"
            editandcontinue "On"
            defines { "LE_DEBUG", "LUMINA_DEBUG", "_DEBUG", "DEBUG" }

        filter "configurations:Development"
            targetsuffix "-Development"
            optimize "Speed"
            symbols "On"
            runtime "Release"
            linktimeoptimization "On"
            defines { "NDEBUG", "LE_DEVELOPMENT", "LUMINA_DEVELOPMENT" }

        filter "configurations:Shipping"
            targetsuffix "-Shipping"
            linktimeoptimization "On"
            optimize "Full"
            symbols "Off"
            runtime "Release"
            defines { "NDEBUG", "LE_SHIPPING", "LUMINA_SHIPPING" }
            removedefines { "TRACY_ENABLE" }
        filter {}
end


local function SetupProject(Def)
    project(Def.Name)
        kind "SharedLib"
        rtti "off"
        staticruntime "Off"
        vectorextensions "AVX2"

        -- Force-include the project's API header, which itself includes
        -- ModuleAPI.h to pull in RUNTIME_API / EDITOR_API / etc.
        forceincludes { Def.Name .. "API.h" }

        -- Reflection: run the project-local ReflectionRunner.bat so premake
        -- processes THIS project's premake5.lua and generates ReflectionUnity.gen.cpp
        -- scoped to this project's headers.
        if Def.Reflection then
            enablereflection "true"
            prebuildcommands
            {
                "\"%{wks.location}\\Tools\\ReflectionRunner.bat\""
            }
        end

        local FilePatterns =
        {
            "Source/**.h",
            "Source/**.cpp",
            "**.lua",
            "**.lproject",
            "**.json",
        }

        if Def.Reflection then
            table.insert(FilePatterns, LuminaConfig.GetReflectionFiles())
        end

        for _, Pattern in ipairs(Def.ExtraFiles or {}) do
            table.insert(FilePatterns, Pattern)
        end

        files(FilePatterns)

        -- Include directories: project source + all engine public includes
        -- (Runtime headers, ThirdParty headers, reflection output, etc.)
        local AllIncludes = { "Source" }
        for _, Dir in ipairs(LuminaConfig.GetPublicIncludeDirectories()) do
            table.insert(AllIncludes, Dir)
        end
        includedirs(AllIncludes)

        if Def.PrivateDefines and #Def.PrivateDefines > 0 then
            defines(Def.PrivateDefines)
        end

        -- Library search paths: the engine's Binaries folder holds Runtime.lib,
        -- Editor.lib, and all third-party static libs (ImGui.lib, EA.lib, etc.).
        local AllLibDirs =
        {
            LuminaConfig.GetTargetDirectory(),
            --LuminaConfig.EnginePath("Binaries/Windows64"),
            LuminaConfig.EnginePath("Engine/Source/ThirdParty/lua"),
        }
        for _, Dir in ipairs(Def.ExtraLibDirs or {}) do
            table.insert(AllLibDirs, Dir)
        end
        libdirs(AllLibDirs)

        -- Links: engine Runtime + third-party deps + any user extras.
        --
        -- The engine applies a configuration-specific target suffix
        -- ("-Debug", "-Development", "-Shipping") to every .lib it produces,
        -- so the actual file names on disk are e.g. "Runtime-Debug.lib".
        -- Inside the engine workspace Premake resolves those suffixes
        -- automatically because it knows "Runtime" is a sibling project.
        -- External workspaces have no such knowledge, so we must emit the
        -- suffixed lib names explicitly per configuration.
        local BaseLinks = { "Runtime" }
        for _, Mod in ipairs(Def.ModuleDependencies or {}) do
            table.insert(BaseLinks, Mod)
        end
        for _, Dep in ipairs(Def.Dependencies) do
            table.insert(BaseLinks, Dep)
        end

        local function WithSuffix(Libs, Suffix)
            local Out = {}
            for _, Lib in ipairs(Libs) do
                table.insert(Out, Lib .. Suffix)
            end
            return Out
        end

        filter "configurations:Debug"
            links(WithSuffix(BaseLinks, "-Debug"))
        filter "configurations:Development"
            links(WithSuffix(BaseLinks, "-Development"))
        filter "configurations:Shipping"
            links(WithSuffix(BaseLinks, "-Shipping"))
        filter {}

        -- Raw extra links are passed through verbatim (user is responsible
        -- for specifying the full name including any suffix/extension).
        if Def.ExtraLinks and #Def.ExtraLinks > 0 then
            links(Def.ExtraLinks)
        end

        -- Editor platform: link the Editor module and expose its headers.
        -- Editor also gets the configuration suffix.
        local EditorBaseLinks = { "Editor" }
        for _, Mod in ipairs(Def.EditorModuleDependencies or {}) do
            table.insert(EditorBaseLinks, Mod)
        end

        filter { "platforms:Editor", "configurations:Debug" }
            links(WithSuffix(EditorBaseLinks, "-Debug"))
        filter { "platforms:Editor", "configurations:Development" }
            links(WithSuffix(EditorBaseLinks, "-Development"))
        filter { "platforms:Editor", "configurations:Shipping" }
            links(WithSuffix(EditorBaseLinks, "-Shipping"))
        filter {}

        filter "platforms:Editor"
            includedirs { LuminaConfig.EnginePath("Engine/Editor/Source") }
        filter {}

        linkoptions { "/NODEFAULTLIB:LIBCMT" }
end


---@param Def table Game project definition
function LuminaGameProject(Def)
    assert(Def.Name, "LuminaGameProject: Name is required")
    assert(LuminaConfig, "LuminaGameProject: BuildScripts/Dependencies must be included first")

    -- Defaults
    if Def.Reflection == nil then
        Def.Reflection = true
    end
    Def.Dependencies = Def.Dependencies or DefaultGameDependencies

    SetupWorkspace(Def)
    SetupProject(Def)
end
