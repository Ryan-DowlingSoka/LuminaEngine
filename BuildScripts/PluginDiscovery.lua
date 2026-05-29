--[[
    Lumina Plugin Discovery

    Scans a "Plugins" root for <PluginName>/<PluginName>.lua build files and
    include()s each so its projects join the surrounding workspace. Called
    from both the engine root premake5.lua (for Engine/Plugins/) and
    LuminaGameProject (for <Project>/Plugins/) so plugins appear in the
    matching .sln automatically.

    A plugin's build file is the matching-name .lua next to its .lplugin
    descriptor (e.g. GameplayExtras/GameplayExtras.lua). Anything else under
    the plugin root (extra .lua files, headers, content) is ignored by
    discovery; the plugin's own build file decides what to include.
]]

assert(LuminaConfig, "PluginDiscovery.lua: include BuildScripts/Dependencies first")
include(path.join(_SCRIPT_DIR, "Plugin.lua"))


---@param Root string Absolute path to a Plugins folder (or nil/empty no-op)
function LuminaDiscoverPlugins(Root)
    if not Root or Root == "" then
        return
    end
    if not os.isdir(Root) then
        return
    end

    -- Each immediate subdir is a candidate plugin. We look for the
    -- conventional <PluginName>/<PluginName>.lua build file; if absent,
    -- the plugin has no native code (content-only) and we just skip it
    -- here -- the runtime side still picks up the .lplugin and mounts
    -- its content folder.
    for _, Entry in ipairs(os.matchdirs(path.join(Root, "*"))) do
        local Name = path.getname(Entry)
        local BuildFile = path.join(Entry, Name .. ".lua")
        if os.isfile(BuildFile) then
            -- include() runs the file in its own _SCRIPT_DIR scope, so
            -- LuminaPlugin / LuminaPluginModule pick up the right
            -- PluginRoot via _SCRIPT_DIR.
            include(BuildFile)
        end
    end
end


-- Discover the conventional Engine/Plugins/ root, called from the engine
-- premake5.lua. External game premake5.lua should call LuminaDiscoverPlugins
-- with their project's own Plugins/ path.
function LuminaDiscoverEnginePlugins()
    local EnginePluginsRoot = LuminaConfig.EnginePath("Engine/Plugins")
    LuminaDiscoverPlugins(EnginePluginsRoot)
end
