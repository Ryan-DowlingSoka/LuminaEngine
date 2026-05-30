-- LuminaModule() declares an engine module with API-macro handling, include propagation, and dependency resolution.

LuminaModules = LuminaModules or {}


-- Resolve a public include dir to an absolute path; "." means the module's base directory.
local function ResolveIncludePath(BasePath, IncDir)
    if IncDir == "." then
        return BasePath
    end
    return path.join(BasePath, IncDir)
end


-- Collect public include dirs a module exposes to dependents: its own, its third-party deps', and the same recursively for module deps.
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


-- Collect public defines a module exposes to dependents: its own, its third-party deps', and the same recursively for module deps.
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

    -- Reflected module's generated headers must be visible to dependents; use an absolute engine path (not %{prj.name}) so dependents still resolve THIS module's output.
    if Def.Reflection then
        table.insert(Def.ResolvedPublicIncludes,
            LuminaConfig.EnginePath(path.join("Intermediates/Reflection", Def.Name)))
    end

    -- Register before project() so later-included modules can reference this one.
    LuminaModules[Def.Name] = Def

    project(Def.Name)
        kind(Def.Kind)
        rtti "off"
        staticruntime "Off"
        vectorextensions "AVX" -- See Workspace.lua: AVX2 #UD-crashes on CPUs without it.

    -- Monolithic Shipping: re-kind SharedLibs to StaticLib so the exe can WHOLEARCHIVE-link them; apps/tools/StaticLibs unaffected. See LUMINA_MONOLITHIC.
    if Def.Kind == "SharedLib" then
        filter "configurations:Shipping"
            kind "StaticLib"
        filter {}
    end

    -- Remove platforms if requested (e.g. Editor removes "Game")
    if Def.RemovePlatforms then
        removeplatforms(Def.RemovePlatforms)
    end

    -- PCH header is force-included ahead of ModuleAPI.h so /Yu finds it first in every TU even when the .cpp doesn't write `#include "pch.h"`.
    if Def.PCH then
        pchheader(Def.PCH.Header)
        pchsource(Def.PCH.Source)
        forceincludes { Def.PCH.Header, "ModuleAPI.h" }
    else
        forceincludes { "ModuleAPI.h" }
    end

    -- Depend on ReflectionGen (workspace-shared Utility) so the Reflector prebuild fires once per build instead of per reflected module.
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

    -- Every image must compile its own EASTLImpl.cpp for per-DLL eastl::allocator decoration; Runtime globs it, others add explicitly. Monolithic Shipping strips it (the exe owns the single definition).
    if (Def.Kind == "SharedLib" or Def.Kind == "WindowedApp" or Def.Kind == "ConsoleApp")
        and not Def.RootFiles then
        files { LuminaConfig.GetEASTLImplFile() }
    end

    if Def.Kind == "SharedLib" then
        filter "configurations:Shipping"
            removefiles { LuminaConfig.GetEASTLImplFile() }
            -- Runtime globbed the file via its relative path, which the absolute removefiles above misses; list the relative variant too.
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

    -- Resolve third-party deps once into (includes, defines, links); transitive and deduplicated via the registry.
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

    -- Link Tracy only in configs where profiling is active (LuminaOptions); strip it elsewhere.
    for _, Cfg in ipairs({ "Debug", "Development", "Shipping" }) do
        if not LuminaOptions.IsActive("Tracy", Cfg) then
            filter("configurations:" .. Cfg)
                removelinks { "Tracy" }
            filter {}
        end
    end

    -- Monolithic Shipping: MSBuild's LinkLibraryDependencies bakes referenced libs' .objs into each .lib, so WHOLEARCHIVE would duplicate them (LNK2005).
    -- Strip third-party AND cross-module links from SharedLibs; the exe links each module and the union of third-party deps exactly once.
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
