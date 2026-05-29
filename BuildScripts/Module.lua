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


-- Collect all public include directories a module exposes to its dependents:
-- the module's own public includes, the public includes of the third-party
-- libraries its headers expose (PUBLIC dependency semantics), and the same
-- recursively for its module dependencies.
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

    -- This module's own public includes
    for _, Dir in ipairs(Mod.ResolvedPublicIncludes or {}) do
        table.insert(Result, Dir)
    end

    -- Third-party headers this module exposes through its public API
    local ThirdPartyIncludes = LuminaThirdParty.Resolve(Mod.Dependencies or {})
    for _, Dir in ipairs(ThirdPartyIncludes) do
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


-- Collect all public defines a module exposes to its dependents: its own
-- PublicDefines, the public defines of its third-party dependencies, and the
-- same recursively for its module dependencies.
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

    local _, ThirdPartyDefines = LuminaThirdParty.Resolve(Mod.Dependencies or {})
    for _, Def in ipairs(ThirdPartyDefines) do
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

    -- A reflected module's generated headers must be visible to its dependents.
    -- Use an absolute engine path (not the per-project %{prj.name} token) so it
    -- still points at THIS module's reflection output when a dependent resolves
    -- it. Doubles as this module's own generated-header include.
    if Def.Reflection then
        table.insert(Def.ResolvedPublicIncludes,
            LuminaConfig.EnginePath(path.join("Intermediates/Reflection", Def.Name)))
    end

    -- Register in the global module table before project() call
    -- so that modules included later can reference this one
    LuminaModules[Def.Name] = Def

    project(Def.Name)
        kind(Def.Kind)
        rtti "off"
        staticruntime "Off"
        vectorextensions "AVX" -- See Workspace.lua: AVX2 #UD-crashes on CPUs without it.

    -- Monolithic Shipping: every SharedLib gets re-kinded to StaticLib so
    -- the final exe (Lumina) can WHOLEARCHIVE-link them. WindowedApp /
    -- ConsoleApp / Utility / StaticLib are unaffected; the exe stays
    -- an exe, build tools stay tools. See LUMINA_MONOLITHIC.
    if Def.Kind == "SharedLib" then
        filter "configurations:Shipping"
            kind "StaticLib"
        filter {}
    end

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

    -- EASTL allocator binding: every image (SharedLib/WindowedApp/ConsoleApp)
    -- that uses EASTL containers must compile its own copy of EASTLImpl.cpp
    -- so eastl::allocator gets defined with the right per-DLL decoration.
    -- Runtime picks the file up via its **.cpp glob; everyone else needs an
    -- explicit `files {}` add. In monolithic Shipping the SharedLibs above
    -- become StaticLibs and the final exe (Lumina, WindowedApp) owns the
    -- single definition -- strip the file from every other Shipping target.
    if (Def.Kind == "SharedLib" or Def.Kind == "WindowedApp" or Def.Kind == "ConsoleApp")
        and not Def.RootFiles then
        files { LuminaConfig.GetEASTLImplFile() }
    end

    if Def.Kind == "SharedLib" then
        filter "configurations:Shipping"
            removefiles { LuminaConfig.GetEASTLImplFile() }
            -- Runtime (RootFiles=true) globbed the file in via its relative
            -- path; the absolute removefiles above doesn't always catch that
            -- so list the relative variant too.
            removefiles { "Memory/EASTLImpl.cpp" }
        filter {}
    end

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

    -- Resolve this module's third-party dependencies once into (includes,
    -- defines, links). Transitive and deduplicated via the registry.
    local ThirdPartyIncludes, ThirdPartyDefines, ThirdPartyLinks =
        LuminaThirdParty.Resolve(Def.Dependencies)

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

    -- This module's own third-party dependency includes
    for _, Dir in ipairs(ThirdPartyIncludes) do
        table.insert(AllIncludes, Dir)
    end

    includedirs(AllIncludes)

    local AllDefines = {}

    -- Private defines
    for _, Def_ in ipairs(Def.PrivateDefines) do
        table.insert(AllDefines, Def_)
    end

    -- Public defines required by this module's third-party dependencies
    for _, Def_ in ipairs(ThirdPartyDefines) do
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

    -- Third-party / external dependencies (transitive linkable set from the
    -- registry; header-only libs contribute nothing here)
    for _, Lib in ipairs(ThirdPartyLinks) do
        table.insert(AllLinks, Lib)
    end

    -- Extra raw library links
    for _, Lib in ipairs(Def.ExtraLinks) do
        table.insert(AllLinks, Lib)
    end

    if #AllLinks > 0 then
        links(AllLinks)
    end

    -- Tracy links only in the configurations where profiling is active
    -- (LuminaOptions). Remove it everywhere else so a "Tracy off" or
    -- Shipping-only-disabled build doesn't pull in the profiler library.
    for _, Cfg in ipairs({ "Debug", "Development", "Shipping" }) do
        if not LuminaOptions.IsActive("Tracy", Cfg) then
            filter("configurations:" .. Cfg)
                removelinks { "Tracy" }
            filter {}
        end
    end

    -- Monolithic Shipping: SharedLib modules become StaticLib and the
    -- final exe (Lumina) WHOLEARCHIVE-links them. With MSBuild's
    -- LinkLibraryDependencies=True (the default for VS C++ projects),
    -- the LIB tool bakes referenced libs' .objs into the resulting .lib
    -- when archiving. WHOLEARCHIVE then pulls those baked-in .objs into
    -- the exe N times -> LNK2005 duplicate symbols. Strip BOTH the
    -- third-party links AND the cross-module links from SharedLib
    -- modules in Shipping; the Lumina exe links each engine module once
    -- and the union of third-party deps once, end of duplicates.
    if Def.Kind == "SharedLib" then
        local StripInShipping = {}
        for _, Lib in ipairs(ThirdPartyLinks) do
            table.insert(StripInShipping, Lib)
        end
        for _, Dep in ipairs(Def.ModuleDependencies) do
            table.insert(StripInShipping, Dep)
        end
        for _, Dep in ipairs(Def.EditorModuleDependencies) do
            table.insert(StripInShipping, Dep)
        end
        if #StripInShipping > 0 then
            filter "configurations:Shipping"
                removelinks(StripInShipping)
            filter {}
        end
    end

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
