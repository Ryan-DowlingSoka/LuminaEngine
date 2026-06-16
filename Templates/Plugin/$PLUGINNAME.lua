-- Build script for the $PLUGINNAME plugin; auto-discovered by LuminaDiscoverPlugins.
LuminaPlugin({
    Name = "$PLUGINNAME",
})

-- Runtime module: loaded in both the editor and packaged game. Put gameplay
-- here (components, systems, reflected types).
LuminaPluginModule({
    Plugin = "$PLUGINNAME",
    Name   = "$RUNTIMEMODULE",
    Type   = "Runtime",
    ModuleDependencies = { "Runtime" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy",
    },
})

-- Editor module: editor-only, stripped from packaged builds. Links the Editor module and this plugin's Runtime module.
LuminaPluginModule({
    Plugin = "$PLUGINNAME",
    Name   = "$EDITORMODULE",
    Type   = "Editor",
    ModuleDependencies = { "Runtime", "$RUNTIMEMODULE" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy",
    },
})
