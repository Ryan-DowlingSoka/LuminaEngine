--[[
    Lumina Plugin Build System

    LuminaPlugin() declares a plugin group: a folder under Engine/Plugins/
    (or <Project>/Plugins/) containing a .lplugin descriptor and one-or-more
    code modules. Each module is its own SharedLib so it can be loaded
    independently (e.g. Editor module skipped in shipping builds).

    The descriptor (.lplugin) is the runtime source of truth — Build.lua
    only handles compilation. Keep the two in sync: every module listed in
    .lplugin needs a matching LuminaPluginModule() call here.

    Usage in <PluginRoot>/<PluginName>.lua:

        LuminaPlugin({
            Name = "MyPlugin",
            -- Optional: extra third-party deps shared by all modules.
            -- Per-module deps go in LuminaPluginModule.
            SharedDependencies = { "ImGui" },
        })

        LuminaPluginModule({
            Plugin = "MyPlugin",
            Name   = "MyPluginRuntime",
            Type   = "Runtime",
            ModuleDependencies = { "Runtime" },
        })

        LuminaPluginModule({
            Plugin = "MyPlugin",
            Name   = "MyPluginEditor",
            Type   = "Editor",
            ModuleDependencies = { "Runtime" },
            EditorModuleDependencies = { "Editor" },
        })

    The module's .cpp files live under <PluginRoot>/Source/<ModuleName>/
    and its API header at <PluginRoot>/Source/<ModuleName>/<ModuleName>API.h.
    See Engine/Plugins/GameplayExtras/ for the canonical layout.
]]

assert(LuminaConfig, "Plugin.lua: include BuildScripts/Dependencies first")

-- Plugin modules delegate to LuminaModule for force-includes, reflection
-- wiring, EASTL impl injection, etc. The engine workspace loads Module.lua
-- itself, but external game premake5.lua files don't — include here so
-- LuminaPluginModule works regardless of who called us.
include(path.join(_SCRIPT_DIR, "Module.lua"))

-- Global plugin registry. Modules register themselves into their plugin so
-- discovery / debugging / future cooking integration has one place to look.
LuminaPlugins = LuminaPlugins or {}


---@param Def table Plugin metadata
function LuminaPlugin(Def)
    assert(Def.Name, "LuminaPlugin: Name is required")

    Def.SharedDependencies       = Def.SharedDependencies       or {}
    Def.SharedModuleDependencies = Def.SharedModuleDependencies or {}
    Def.Modules                  = {} -- filled in by LuminaPluginModule

    -- _SCRIPT_DIR is the dir of the lua file currently being executed by
    -- premake (this plugin's <Name>.lua), which is the plugin root.
    Def.PluginRoot = _SCRIPT_DIR

    LuminaPlugins[Def.Name] = Def
end


---@param Def table Plugin module definition
function LuminaPluginModule(Def)
    assert(Def.Plugin, "LuminaPluginModule: Plugin is required")
    assert(Def.Name,   "LuminaPluginModule: Name is required")
    assert(Def.Type,   "LuminaPluginModule: Type is required (Runtime/Editor/Developer)")

    local Plugin = LuminaPlugins[Def.Plugin]
    assert(Plugin, "LuminaPluginModule: plugin '" .. Def.Plugin .. "' not declared (call LuminaPlugin first)")

    Def.ModuleDependencies       = Def.ModuleDependencies       or {}
    Def.EditorModuleDependencies = Def.EditorModuleDependencies or {}
    Def.Dependencies             = Def.Dependencies             or {}
    Def.PublicDefines            = Def.PublicDefines            or {}
    Def.PrivateDefines           = Def.PrivateDefines           or {}
    Def.PublicIncludeDirs        = Def.PublicIncludeDirs        or {}
    Def.ExtraFiles               = Def.ExtraFiles               or {}

    -- Reflection on by default. Most plugins ship reflected types
    -- (components, assets, classes) and the Reflector itself is a fast
    -- per-module no-op when there's nothing to find.
    if Def.Reflection == nil then
        Def.Reflection = true
    end

    -- Compose effective dependencies: plugin shared + module-specific.
    local AllDeps = {}
    for _, D in ipairs(Plugin.SharedDependencies) do          table.insert(AllDeps, D) end
    for _, D in ipairs(Def.Dependencies)        do            table.insert(AllDeps, D) end

    local AllModuleDeps = {}
    for _, D in ipairs(Plugin.SharedModuleDependencies) do    table.insert(AllModuleDeps, D) end
    for _, D in ipairs(Def.ModuleDependencies)         do     table.insert(AllModuleDeps, D) end

    Def.AllDependencies       = AllDeps
    Def.AllModuleDependencies = AllModuleDeps

    table.insert(Plugin.Modules, Def)

    -- Editor-typed modules link against Editor automatically.
    if Def.Type == "Editor" then
        local HasEditorLink = false
        for _, M in ipairs(Def.EditorModuleDependencies) do
            if M == "Editor" then HasEditorLink = true break end
        end
        if not HasEditorLink then
            table.insert(Def.EditorModuleDependencies, "Editor")
        end
    end

    -- Delegate to the same LuminaModule used by engine modules so we get
    -- identical force-includes, reflection wiring, EASTL impl injection,
    -- third-party linking, etc. We just steer the output and source dirs
    -- into the plugin's tree.
    LuminaModule({
        Name                     = Def.Name,
        Kind                     = "SharedLib",
        Reflection               = Def.Reflection,
        ModuleDependencies       = AllModuleDeps,
        EditorModuleDependencies = Def.EditorModuleDependencies,
        Dependencies             = AllDeps,
        PublicIncludeDirs        = Def.PublicIncludeDirs,
        PrivateIncludeDirs       = Def.PrivateIncludeDirs,
        PublicDefines            = Def.PublicDefines,
        PrivateDefines           = Def.PrivateDefines,
        ExtraFiles               = Def.ExtraFiles,
    })

    -- Override the source set: LuminaModule globbed Source/**, but a
    -- multi-module plugin shares Source/ across all of its modules. Drop
    -- the blanket glob FIRST so it can't strip our per-module adds, then
    -- add only this module's slice.
    location(Plugin.PluginRoot)
    removefiles { "Source/**.h", "Source/**.cpp" }
    files {
        path.join(Plugin.PluginRoot, "Source", Def.Name, "**.h"),
        path.join(Plugin.PluginRoot, "Source", Def.Name, "**.cpp"),
    }
    includedirs { path.join(Plugin.PluginRoot, "Source", Def.Name) }

    -- Output the module DLL where the runtime expects it: under the
    -- plugin's own Binaries/<Platform>/ folder, matching the path
    -- FPlugin::ResolveModuleBinaryPath computes.
    targetdir(path.join(Plugin.PluginRoot, "Binaries", LuminaConfig.OutputDirectory))
    objdir   (path.join(Plugin.PluginRoot, "Intermediates", "Obj", LuminaConfig.OutputDirectory, "%{prj.name}"))

    -- Monolithic Shipping: plugin modules become StaticLib (see
    -- Module.lua) and the Lumina exe needs to WHOLEARCHIVE every one.
    -- /WHOLEARCHIVE with a bare lib name requires the lib on the
    -- linker's default search path; consolidating Shipping plugin libs
    -- into the engine's Binaries dir means the existing LIBPATH (set
    -- by Lumina's link to Runtime) finds them. Modular builds still
    -- ship each plugin's DLL alongside its descriptor as before.
    filter "configurations:Shipping"
        targetdir(LuminaConfig.GetTargetDirectory())
    filter {}

    -- Editor-typed modules are stripped from non-editor builds. We do
    -- this by removing the Editor module project from the Game platform;
    -- a Game-platform build of the plugin will simply not include it.
    if Def.Type == "Editor" then
        removeplatforms { "Game" }
    end
end
