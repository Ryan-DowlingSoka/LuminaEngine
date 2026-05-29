# $PLUGINNAME

$PLUGINDESCRIPTION

A Lumina plugin scaffolded from the editor. It ships two modules:

- **$RUNTIMEMODULE** (`Runtime`) — gameplay code loaded in both the editor and
  packaged game. Add reflected components/systems, Lua bindings, etc. here.
- **$EDITORMODULE** (`Editor`) — editor-only customizations, stripped from
  packaged builds. Register asset editor tools, property customizations, and
  menus here.

## Layout

```
$PLUGINNAME/
├── $PLUGINNAME.lplugin            Descriptor (modules, loading phases)
├── $PLUGINNAME.lua                Build script (premake)
└── Source/
    ├── $RUNTIMEMODULE/
    └── $EDITORMODULE/
```

## Building

The plugin is discovered by the owning project's premake generation. After
creating it, regenerate the project's Visual Studio solution (run the project's
`GenerateProject.bat`, or use the editor's New Plugin flow which does this for
you), then rebuild. The new module DLLs load on the next editor launch.

## Enabling / disabling

Toggle the plugin per-project in the editor's **Plugin Browser**, which writes
an entry into the project's `.lproject`.
