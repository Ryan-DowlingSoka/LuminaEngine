--[[
    Lumina Game Project Build System

    Declares an EXTERNAL game project that links against a pre-built Lumina
    engine install. Unlike LuminaModule() (engine-internal modules), this also
    creates the whole premake workspace, since a game project ships its own
    .sln separate from the engine's.

    The workspace settings come from the SAME LuminaWorkspaceSettings() the
    engine uses, and third-party includes are resolved from the SAME registry,
    so a game compiles engine/third-party headers with identical preprocessor
    state. There is no copy of the engine's defines/filters here to drift.

    Usage (in an external project's premake5.lua):

        local LuminaDir = os.getenv("LUMINA_DIR")
        include(path.join(LuminaDir, "BuildScripts/Dependencies"))
        include(path.join(LuminaDir, "BuildScripts/Actions/Reflection"))
        include(path.join(LuminaDir, "BuildScripts/GameProject"))

        LuminaGameProject({
            Name = "MyGame",
            -- Third-party libs whose SYMBOLS this project links directly.
            -- Engine-exposed headers (glm, eastl, ...) are made available
            -- automatically; you only list what you actually link.
            Dependencies = { "ImGui", "EA", "Tracy", "RPMalloc", "EnkiTS", "Luau" },
        })

    Required:
        Name                     - Project / workspace name

    Optional:
        Dependencies             - Third-party libs to link (defaults to the
                                   standard game set below).
        ModuleDependencies       - Extra engine modules to link (Runtime always).
        EditorModuleDependencies - Engine modules linked only on Editor (Editor
                                   always added on the Editor platform).
        Reflection               - Enable reflection generation (default true).
        PrivateDefines           - Extra defines for this project only.
        ExtraFiles               - Extra file patterns to include.
        ExtraLibDirs             - Extra library search paths.
        ExtraLinks               - Extra raw library names (verbatim).
]]

assert(LuminaConfig, "GameProject.lua: include BuildScripts/Dependencies first")
include(path.join(_SCRIPT_DIR, "Workspace.lua"))
include(path.join(_SCRIPT_DIR, "PluginDiscovery.lua"))


-- Standard third-party dependencies every Lumina game project links. Engine
-- headers transitively expose far more (glm, eastl, ...), but those are
-- header-only / already absorbed into the engine DLLs; this is the minimum a
-- game TU links to satisfy engine template instantiations.
local DefaultGameDependencies =
{
    "ImGui",
    "RPMalloc",
    "EA",
    "EnkiTS",
    "Tracy",
    "Luau",
}


local function SetupProject(Def)
    project(Def.Name)
        kind "SharedLib"
        rtti "off"
        staticruntime "Off"
        vectorextensions "AVX" -- See Workspace.lua: AVX2 #UD-crashes on CPUs without it.

        -- Force-include the project's API header, which includes ModuleAPI.h to
        -- pull in RUNTIME_API / EDITOR_API / etc.
        forceincludes { Def.Name .. "API.h" }

        -- Ensure the matching-config engine libraries exist before building
        -- this game project. First-time builds against an unbuilt engine
        -- config (e.g. game Debug against an engine that's only been built
        -- Development) used to fail with "Runtime-Debug.lib not found"; the
        -- script transparently invokes msbuild on Lumina.sln and is a no-op
        -- once the libs exist. See BuildScripts/EnsureEngineBuilt.bat.
        local LuminaDirForBat = os.getenv("LUMINA_DIR")
        if LuminaDirForBat then
            local EnsureBat = '"' .. path.translate(path.join(LuminaDirForBat, "BuildScripts", "EnsureEngineBuilt.bat"), "\\") .. '"'

            -- Pass $(Platform) through so an Editor-platform game build
            -- triggers the Editor-platform engine build, and likewise for
            -- Game. Without the platform arg, packaged-game builds (Game
            -- platform) would silently fall back to building Editor libs
            -- they can't link against.
            filter "configurations:Debug"
                prebuildcommands { EnsureBat .. ' Debug "$(Platform)"' }
            filter "configurations:Development"
                prebuildcommands { EnsureBat .. ' Development "$(Platform)"' }
            filter "configurations:Shipping"
                prebuildcommands { EnsureBat .. ' Shipping "$(Platform)"' }
            filter {}
        end

        -- Reflection: run the project-local ReflectionRunner.bat so premake
        -- processes THIS project's premake5.lua and generates its own
        -- ReflectionUnity.gen.cpp scoped to this project's headers.
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

        -- Single canonical EASTL allocator binding from the engine install;
        -- the game DLL needs its own compiled copy so eastl::allocator has the
        -- right `dllimport` decoration in this image.
        table.insert(FilePatterns, LuminaConfig.GetEASTLImplFile())

        files(FilePatterns)

        -- Includes: project source + this project's generated reflection +
        -- engine Runtime headers/reflection + every third-party header the
        -- engine's public API exposes + this project's own third-party deps.
        local AllIncludes = { "Source" }
        table.insert(AllIncludes, path.join("%{wks.location}", "Intermediates/Reflection", Def.Name))
        for _, Dir in ipairs(LuminaConfig.GetEngineRuntimeIncludes()) do
            table.insert(AllIncludes, Dir)
        end
        for _, Dir in ipairs(LuminaThirdParty.IncludesOf(LuminaThirdParty.RuntimePublicDeps)) do
            table.insert(AllIncludes, Dir)
        end
        for _, Dir in ipairs(LuminaThirdParty.IncludesOf(Def.Dependencies)) do
            table.insert(AllIncludes, Dir)
        end
        includedirs(AllIncludes)

        if Def.PrivateDefines and #Def.PrivateDefines > 0 then
            defines(Def.PrivateDefines)
        end

        -- Library search paths: the engine's Binaries folder holds Runtime.lib,
        -- Editor.lib and all third-party static libs.
        local AllLibDirs =
        {
            LuminaConfig.GetTargetDirectory(),
            LuminaConfig.EnginePath("Engine/Source/ThirdParty/lua"),
        }
        for _, Dir in ipairs(Def.ExtraLibDirs or {}) do
            table.insert(AllLibDirs, Dir)
        end
        libdirs(AllLibDirs)

        -- Links: engine Runtime + the linkable closure of the project's
        -- third-party deps + any user extras.
        --
        -- The engine appends a configuration-specific suffix ("-Debug", etc.)
        -- to every .lib it produces, so we emit suffixed names per config.
        -- Inside the engine workspace premake resolves these automatically;
        -- external workspaces have no such knowledge.
        local _, _, ThirdPartyLinks = LuminaThirdParty.Resolve(Def.Dependencies)

        local BaseLinks = { "Runtime" }
        for _, Mod in ipairs(Def.ModuleDependencies or {}) do
            table.insert(BaseLinks, Mod)
        end
        for _, Lib in ipairs(ThirdPartyLinks) do
            table.insert(BaseLinks, Lib)
        end

        local function WithSuffix(Libs, Suffix)
            local Out = {}
            for _, Lib in ipairs(Libs) do
                table.insert(Out, Lib .. Suffix)
            end
            return Out
        end

        -- Tracy links only where profiling is active (LuminaOptions). Strip it
        -- from the link set for every other configuration.
        local function StripTracy(Libs)
            local Out = {}
            for _, Lib in ipairs(Libs) do
                if Lib ~= "Tracy" then
                    table.insert(Out, Lib)
                end
            end
            return Out
        end

        for _, Cfg in ipairs({ "Debug", "Development", "Shipping" }) do
            local Links = BaseLinks
            if not LuminaOptions.IsActive("Tracy", Cfg) then
                Links = StripTracy(BaseLinks)
            end
            filter("configurations:" .. Cfg)
                links(WithSuffix(Links, "-" .. Cfg))
            filter {}
        end

        if Def.ExtraLinks and #Def.ExtraLinks > 0 then
            links(Def.ExtraLinks)
        end

        -- Visual Studio debugger wiring: F5 in the game's .sln launches the
        -- editor with this project pre-loaded, so breakpoints in the game DLL
        -- are hit as soon as IMPLEMENT_MODULE runs. Mirrors the UE workflow.
        --
        -- Engine binaries are resolved at generate time via LUMINA_DIR; the
        -- project's own premake5.lua already asserts that variable is set.
        local LuminaDir = os.getenv("LUMINA_DIR")
        if LuminaDir then
            local EngineBin = path.join(LuminaDir, "Binaries", "Windows64")
            local LprojPath = path.join("%{wks.location}", Def.Name .. ".lproject")

            debugdir(EngineBin)
            debugargs { "--Project=\"" .. LprojPath .. "\"" }

            filter "configurations:Debug"
                debugcommand(path.join(EngineBin, "Lumina-Debug.exe"))
            filter "configurations:Development"
                debugcommand(path.join(EngineBin, "Lumina-Development.exe"))
            filter "configurations:Shipping"
                debugcommand(path.join(EngineBin, "Lumina-Shipping.exe"))
            filter {}
        end

        -- Editor platform: link the Editor module (+ extras), expose its
        -- headers and the editor-only third-party the engine exposes.
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
            local EditorIncludes = LuminaConfig.GetEngineEditorIncludes()
            for _, Dir in ipairs(LuminaThirdParty.IncludesOf(LuminaThirdParty.EditorPublicDeps)) do
                table.insert(EditorIncludes, Dir)
            end
            includedirs(EditorIncludes)
        filter {}

        linkoptions { "/NODEFAULTLIB:LIBCMT" }
end


---@param Def table Game project definition
function LuminaGameProject(Def)
    assert(Def.Name, "LuminaGameProject: Name is required")
    assert(LuminaConfig, "LuminaGameProject: BuildScripts/Dependencies must be included first")

    if Def.Reflection == nil then
        Def.Reflection = true
    end
    Def.Dependencies = Def.Dependencies or DefaultGameDependencies

    LuminaWorkspaceSettings({
        Name           = Def.Name,
        StartProject   = Def.Name,
        TargetDir      = path.join("%{wks.location}", "Binaries", LuminaConfig.OutputDirectory),
        ObjDir         = path.join("%{wks.location}", "Intermediates", "Obj", LuminaConfig.OutputDirectory, "%{prj.name}"),
        -- Default to Development for game projects — Debug is ~3x slower to
        -- compile and rarely needed for iteration; users can still pick it.
        Configurations = { "Development", "Debug", "Shipping" },
    })

    SetupProject(Def)

    -- Engine plugins ship pre-built with the engine; we don't re-include
    -- them here (they already live in Lumina.sln's "Plugins" group). Only
    -- project-local plugins under <Project>/Plugins/ get added to the
    -- game's .sln. The runtime side loads both kinds at startup.
    group "Plugins"
        LuminaDiscoverPlugins(path.join(_MAIN_SCRIPT_DIR, "Plugins"))
    group ""
end
