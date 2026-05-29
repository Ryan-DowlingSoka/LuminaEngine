-- Build script for the $PLUGINNAME plugin.
-- Discovered automatically by the owning project's premake generation
-- (LuminaDiscoverPlugins scans <Project>/Plugins/*/<Name>.lua).

LuminaPlugin({
    Name = "$PLUGINNAME",
})

-- Runtime module: loaded in both the editor and packaged game. Put gameplay
-- here (components, systems, Lua bindings, reflected types).
LuminaPluginModule({
    Plugin = "$PLUGINNAME",
    Name   = "$RUNTIMEMODULE",
    Type   = "Runtime",
    ModuleDependencies = { "Runtime" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy", "Luau",
    },
})

-- Editor module: editor-only, stripped from packaged (Game-platform) builds.
-- Put editor customizations here (asset editor tools, property customizations,
-- menus). It links the Editor module automatically and the plugin's own
-- Runtime module so it can reference the gameplay types declared above.
LuminaPluginModule({
    Plugin = "$PLUGINNAME",
    Name   = "$EDITORMODULE",
    Type   = "Editor",
    ModuleDependencies = { "Runtime", "$RUNTIMEMODULE" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy", "Luau",
    },
})
