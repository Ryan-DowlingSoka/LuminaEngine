--[[
    Lumina Module Build System

    Provides LuminaModule() for declaring engine modules with automatic
    API macro handling, include propagation, and dependency resolution.

    Usage:
        LuminaModule({
            Name = "MyModule",
            Kind = "SharedLib",
            ModuleDependencies = { "Runtime" },
            Dependencies = { "ImGui", "EA" },
            PublicIncludeDirs = { "." },
            ...
        })
]]

-- Global registry of declared modules for dependency resolution
LuminaModules = LuminaModules or {}


-- Resolve the absolute path for a module's public include directory.
-- If the path is "." it resolves to the module's base directory.
local function ResolveIncludePath(BasePath, IncDir)
    if IncDir == "." then
        return BasePath
    end
    return path.join(BasePath, IncDir)
end


-- Collect all public include directories from a module and its transitive ModuleDependencies
local function CollectPublicIncludes(ModuleName, Visited)
    Visited = Visited or {}
    if Visited[ModuleName] then
        return {}
    end
    Visited[ModuleName] = true

    local Mod = LuminaModules[ModuleName]
    if not Mod then
        return {}
    end

    local Result = {}

    -- Add this module's public includes
    for _, Dir in ipairs(Mod.ResolvedPublicIncludes or {}) do
        table.insert(Result, Dir)
    end

    -- Recurse into module dependencies
    for _, DepName in ipairs(Mod.ModuleDependencies or {}) do
        local DepIncludes = CollectPublicIncludes(DepName, Visited)
        for _, Dir in ipairs(DepIncludes) do
            table.insert(Result, Dir)
        end
    end

    return Result
end


-- Collect all public defines from a module and its transitive ModuleDependencies
local function CollectPublicDefines(ModuleName, Visited)
    Visited = Visited or {}
    if Visited[ModuleName] then
        return {}
    end
    Visited[ModuleName] = true

    local Mod = LuminaModules[ModuleName]
    if not Mod then
        return {}
    end

    local Result = {}

    for _, Def in ipairs(Mod.PublicDefines or {}) do
        table.insert(Result, Def)
    end

    for _, DepName in ipairs(Mod.ModuleDependencies or {}) do
        local DepDefines = CollectPublicDefines(DepName, Visited)
        for _, Def in ipairs(DepDefines) do
            table.insert(Result, Def)
        end
    end

    return Result
end


---@param Def table Module definition table
function LuminaModule(Def)

    -- Validate required fields
    assert(Def.Name, "LuminaModule: Name is required")
    assert(Def.Kind, "LuminaModule: Kind is required")

    -- Defaults
    Def.ModuleDependencies          = Def.ModuleDependencies or {}
    Def.EditorModuleDependencies    = Def.EditorModuleDependencies or {}
    Def.Dependencies                = Def.Dependencies or {}
    Def.PublicIncludeDirs           = Def.PublicIncludeDirs or {}
    Def.PrivateIncludeDirs          = Def.PrivateIncludeDirs or {}
    Def.PublicDefines               = Def.PublicDefines or {}
    Def.PrivateDefines              = Def.PrivateDefines or {}
    Def.ExtraLinks                  = Def.ExtraLinks or {}
    Def.LibDirs                     = Def.LibDirs or {}
    Def.FatalWarnings               = Def.FatalWarnings or {}
    Def.ExtraFiles                  = Def.ExtraFiles or {}

    -- Resolve public include directories to absolute paths for propagation
    Def.ResolvedPublicIncludes = {}
    local BasePath = path.getabsolute(".")
    for _, Dir in ipairs(Def.PublicIncludeDirs) do
        table.insert(Def.ResolvedPublicIncludes, ResolveIncludePath(BasePath, Dir))
    end

    -- For non-RootFiles modules, "Source" is implicitly a public include
    -- so dependents can include this module's headers
    if not Def.RootFiles then
        table.insert(Def.ResolvedPublicIncludes, path.join(BasePath, "Source"))
    end

    -- Register in the global module table before project() call
    -- so that modules included later can reference this one
    LuminaModules[Def.Name] = Def

    project(Def.Name)
        kind(Def.Kind)
        rtti "off"
        staticruntime "Off"
        vectorextensions "AVX2"

    -- Remove platforms if requested (e.g. Editor removes "Game")
    if Def.RemovePlatforms then
        removeplatforms(Def.RemovePlatforms)
    end

    -- Precompiled header. When present, the PCH header is force-included
    -- ahead of ModuleAPI.h so /Yu finds it as the first include in every TU
    -- even when the .cpp doesn't write `#include "pch.h"` itself (Editor's
    -- existing TUs don't). #pragma once makes the explicit-include case
    -- already-present-in-Runtime idempotent.
    if Def.PCH then
        pchheader(Def.PCH.Header)
        pchsource(Def.PCH.Source)
        forceincludes { Def.PCH.Header, "ModuleAPI.h" }
    else
        forceincludes { "ModuleAPI.h" }
    end

    -- Reflection setup
    --
    -- ReflectionGen is a workspace-shared Utility project that fires the
    -- Reflector prebuild exactly once per build. Depending on it (rather
    -- than wiring our own prebuildcommands) avoids paying premake's
    -- workspace-walk startup cost for every reflected module. The lua
    -- Reflection action processes every project with enablereflection=true
    -- in a single pass, so one invocation covers all reflected modules.
    if Def.Reflection then
        dependson { "Reflector", "ReflectionGen" }
        enablereflection "true"
    end

    local FilePatterns = {
        "Source/**.h",
        "Source/**.cpp",
        "**.lua",
    }

    -- Runtime is structured differently - files are at the root, not in Source/
    if Def.RootFiles then
        FilePatterns = {
            "**.cpp",
            "**.h",
            "**.lua",
        }
        if Def.Reflection then
            table.insert(FilePatterns, "**.slang")
        end
    end

    -- Add reflection generated files for SharedLib modules with reflection
    if Def.Reflection then
        table.insert(FilePatterns, LuminaConfig.GetReflectionFiles())
    end

    for _, Pattern in ipairs(Def.ExtraFiles) do
        table.insert(FilePatterns, Pattern)
    end

    files(FilePatterns)

    -- Disable PCH for third-party sources
    if Def.PCH then
        filter "files:Engine/Source/ThirdParty/**.cpp"
            enablepch "Off"
        filter {}

        filter "files:Engine/Source/ThirdParty/**.c"
            enablepch "Off"
            language "C"
        filter {}
    end

    local AllIncludes = {}

    -- Private includes (this module only)
    for _, Dir in ipairs(Def.PrivateIncludeDirs) do
        table.insert(AllIncludes, Dir)
    end

    -- This module's own public includes
    for _, Dir in ipairs(Def.ResolvedPublicIncludes) do
        table.insert(AllIncludes, Dir)
    end

    -- "Source" directory is implicitly included for non-RootFiles modules
    if not Def.RootFiles then
        table.insert(AllIncludes, "Source")
    end

    -- Public includes from all module dependencies (transitive)
    for _, DepName in ipairs(Def.ModuleDependencies) do
        local DepIncludes = CollectPublicIncludes(DepName)
        for _, Dir in ipairs(DepIncludes) do
            table.insert(AllIncludes, Dir)
        end
    end

    -- Editor module dependencies (same logic, but filtered to Editor platform)
    -- We add these unconditionally here; the link filter handles platform restriction
    for _, DepName in ipairs(Def.EditorModuleDependencies) do
        local DepIncludes = CollectPublicIncludes(DepName)
        for _, Dir in ipairs(DepIncludes) do
            table.insert(AllIncludes, Dir)
        end
    end

    -- Global public include directories (third-party, engine-wide)
    for _, Dir in ipairs(LuminaConfig.GetPublicIncludeDirectories()) do
        table.insert(AllIncludes, Dir)
    end

    includedirs(AllIncludes)

    local AllDefines = {}

    -- Private defines
    for _, Def_ in ipairs(Def.PrivateDefines) do
        table.insert(AllDefines, Def_)
    end

    -- Public defines from dependencies (transitive)
    for _, DepName in ipairs(Def.ModuleDependencies) do
        local DepDefines = CollectPublicDefines(DepName)
        for _, Def_ in ipairs(DepDefines) do
            table.insert(AllDefines, Def_)
        end
    end

    if #AllDefines > 0 then
        defines(AllDefines)
    end

    local AllLinks = {}

    -- Module dependencies (other Lumina modules)
    for _, DepName in ipairs(Def.ModuleDependencies) do
        table.insert(AllLinks, DepName)
    end

    -- Third-party / external dependencies
    for _, Dep in ipairs(Def.Dependencies) do
        table.insert(AllLinks, Dep)
    end

    -- Extra raw library links
    for _, Lib in ipairs(Def.ExtraLinks) do
        table.insert(AllLinks, Lib)
    end

    if #AllLinks > 0 then
        links(AllLinks)
    end

    -- Tracy is a Debug/Development-only profiler. Shipping builds have
    -- TRACY_ENABLE removed (macros become no-ops) and must not link Tracy.
    filter "configurations:Shipping"
        removelinks { "Tracy" }
    filter {}

    -- Editor-only module dependencies
    if #Def.EditorModuleDependencies > 0 then
        filter "platforms:Editor"
            local EditorLinks = {}
            for _, DepName in ipairs(Def.EditorModuleDependencies) do
                table.insert(EditorLinks, DepName)
            end
            links(EditorLinks)
        filter {}
    end
---------------------
    if #Def.LibDirs > 0 then
        libdirs(Def.LibDirs)
    end

    if #Def.FatalWarnings > 0 then
        fatalwarnings(Def.FatalWarnings)
    end

    if Def.Kind == "SharedLib" then
        linkoptions
        {
            "/NODEFAULTLIB:LIBCMT"
        }
    end

    if Def.PrebuildCommands then
        prebuildcommands(Def.PrebuildCommands)
    end

end
