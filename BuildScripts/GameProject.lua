-- LuminaGameProject() declares an external game project (plus its own workspace) that links a pre-built engine install.
-- Reuses the engine's LuminaWorkspaceSettings() and third-party registry so a game compiles with identical preprocessor state.

assert(LuminaConfig, "GameProject.lua: include BuildScripts/Dependencies first")
include(path.join(_SCRIPT_DIR, "Workspace.lua"))
include(path.join(_SCRIPT_DIR, "PluginDiscovery.lua"))


-- Minimum third-party set a game TU links to satisfy engine template instantiations; engine headers expose more (header-only / absorbed into the DLLs).
local DefaultGameDependencies =
{
    "ImGui",
    "RPMalloc",
    "EA",
    "Tracy",
}


local function SetupProject(Def)
    project(Def.Name)
        kind "SharedLib"
        rtti "off"
        staticruntime "Off"
        vectorextensions "AVX" -- See Workspace.lua: AVX2 #UD-crashes on CPUs without it.

        -- Force-include the project's API header (pulls in ModuleAPI.h → RUNTIME_API/EDITOR_API/etc.).
        forceincludes { Def.Name .. "API.h" }

        -- Build the matching-config engine libs first (no-op once they exist); avoids "Runtime-Debug.lib not found" on a first build against an unbuilt config. See EnsureEngineBuilt.bat.
        local LuminaDirForBat = os.getenv("LUMINA_DIR")
        if LuminaDirForBat then
            local EnsureBat = '"' .. path.translate(path.join(LuminaDirForBat, "BuildScripts", "EnsureEngineBuilt.bat"), "\\") .. '"'

            -- Build the matching-platform engine libs before this project so it can link. Editor/Game are
            -- premake PLATFORMS, but with architecture x86_64 premake folds them into the MSBuild configuration
            -- name ("Development Editor") and the project's $(Platform) is just "x64" -- so we can't pass
            -- $(Platform) to EnsureEngineBuilt (it needs Editor/Game). Instead scope per (config, platform) and
            -- emit the literal token into each config's prebuild.
            for _, Cfg in ipairs({ "Debug", "Development", "Shipping" }) do
                for _, Plat in ipairs({ "Editor", "Game" }) do
                    filter { "configurations:" .. Cfg, "platforms:" .. Plat }
                        prebuildcommands { EnsureBat .. ' ' .. Cfg .. ' ' .. Plat }
                end
            end
            filter {}
        end

        -- Run the project-local ReflectionRunner.bat so premake generates this project's own ReflectionUnity.gen.cpp scoped to its headers.
        if Def.Reflection then
            enablereflection "true"
            -- Route the game module's generated C# bindings into the game's Scripts/Generated so they compile
            -- into the Game script assembly (not LuminaSharp.dll), mirroring the plugin routing.
            csharpbindingsdir(path.join(_MAIN_SCRIPT_DIR, "Game", "Scripts", "Generated"))
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

        -- Game DLL compiles its own EASTLImpl.cpp copy so eastl::allocator gets the right dllimport decoration in this image.
        table.insert(FilePatterns, LuminaConfig.GetEASTLImplFile())

        files(FilePatterns)

        -- Includes: project source + generated reflection + engine Runtime headers + engine-exposed third-party + this project's deps.
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

        -- The engine's Binaries folder holds Runtime.lib, Editor.lib, and all third-party static libs.
        local AllLibDirs =
        {
            LuminaConfig.GetTargetDirectory(),
        }
        for _, Dir in ipairs(Def.ExtraLibDirs or {}) do
            table.insert(AllLibDirs, Dir)
        end
        libdirs(AllLibDirs)

        -- Engine Runtime + the linkable closure of the project's deps + user extras; the engine suffixes every .lib per-config ("-Debug" etc.), so we emit suffixed names.
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

        -- Link Tracy only where profiling is active (LuminaOptions); strip it elsewhere.
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

        -- F5 in the game's .sln launches the editor with this project pre-loaded; engine binaries resolved at generate time via LUMINA_DIR.
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
        -- Default to Development for game projects, Debug is ~3x slower to
        -- compile and rarely needed for iteration; users can still pick it.
        Configurations = { "Development", "Debug", "Shipping" },
    })

    SetupProject(Def)

    -- Only project-local plugins under <Project>/Plugins/ get added here; engine plugins ship pre-built in Lumina.sln. Runtime loads both at startup.
    group "Plugins"
        LuminaDiscoverPlugins(path.join(_MAIN_SCRIPT_DIR, "Plugins"))
    group ""
end
