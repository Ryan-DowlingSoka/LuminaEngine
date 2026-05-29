# $PROJECTNAME

A Lumina Engine project.

## Requirements

- The `LUMINA_DIR` environment variable must point at your engine install (set by the engine's `Setup.bat`).
- Visual Studio 2022 with the C++ workload.

## First-time setup

1. Run `GenerateProject.bat` from this folder. This invokes `%LUMINA_DIR%\Tools\premake5.exe vs2022` and writes `$PROJECTNAME.sln`.
2. Open `$PROJECTNAME.sln` in **Visual Studio** or **JetBrains Rider**.
3. Press **F5**.

F5 builds the game DLL (`Binaries\Windows64\$PROJECTNAME-Development.dll`) and launches the Lumina editor with this project pre-loaded. Breakpoints in your game module will hit as soon as `IMPLEMENT_MODULE` runs.

- **Visual Studio** — uses the `.vcxproj.user` settings premake writes (`debugcommand` → `Lumina-Development.exe`).
- **Rider** — uses the shared `.run/Launch Editor (Development).run.xml` (and `(Debug).run.xml`) configurations that ship with this template. Pick one from the configuration dropdown next to the Run button.

## Iterating

- After editing `Source/*.cpp` or `Source/*.h`, just press F5 again — VS rebuilds the DLL and relaunches the editor.
- Content (assets in `Game/Content/`) and Lua scripts hot-reload inside the editor; no rebuild needed.
- New `.h` / `.cpp` files require a re-run of `GenerateProject.bat` (premake globs sources at generate time).

## Project layout

```
$PROJECTNAME.lproject          Project descriptor (name, GUID, plugins)
premake5.lua                   Build script — calls LuminaGameProject()
GenerateProject.bat            One-shot: regenerate the .sln after source changes
Config/GameSettings.json       Per-project engine settings
Source/                        Your C++ module
Game/Content/                  Assets visible to the engine as /Game
Game/Scripts/                  Lua / Luau scripts
Tools/ReflectionRunner.bat     Prebuild step — regenerates reflection code
Intermediates/Reflection/      Generated reflection code (do not edit)
```
