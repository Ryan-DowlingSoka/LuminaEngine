# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LuminaEngine is an educational C++23 game engine using Vulkan, targeting Windows 10/11 (64-bit). It uses Premake5 (not CMake) to generate Visual Studio 2022 solutions.

## Build System

**Premake5** generates the VS2022 solution (`Lumina.sln`). To regenerate project files:
```bash
python BuildScripts/GenerateProjectFiles.py
```

**Initial setup** (downloads ~2GB of dependencies to `External/`):
```bash
python Setup.py
```

**Building:** Open `Lumina.sln` in Visual Studio 2022 and build. There are no CLI build commands — VS is the build tool.

**Configurations:** Debug | Development | Shipping  
**Platforms:** Editor (with tools) | Game (standalone)

**After every pull/merge**, delete `Binaries/`, `Intermediates/`, and `.vs/` before rebuilding. The git hook at `BuildScripts/Hooks/post_merge` does this automatically.

**Reflection pre-build step:** The `Reflector.exe` tool runs before the main build, parsing headers with `REFLECT()` macros and emitting `.generated.h` files into `Intermediates/Reflection/`. If you see "cannot find .generated.h", build twice — VS needs two passes on first setup.

## Architecture

### Module Layout

- `Engine/Source/Runtime/` — Core engine DLL (the majority of code lives here)
- `Engine/Applications/Lumina/` — Main executable (thin entry point)
- `Engine/Applications/Reflector/` — Reflection metadata generator (pre-build tool)
- `Engine/Editor/` — ImGui-based editor module
- `Engine/Sandbox/` — Test/demo project
- `Engine/Resources/Shaders/` — GLSL/Slang shaders

### Core Subsystems (`Engine/Source/Runtime/`)

| Subsystem | Key Purpose |
|-----------|-------------|
| `Core/` | Reflection, base Object type, type system |
| `Renderer/` | Vulkan rendering, RenderGraph, materials |
| `World/Entity/` | ECS via EnTT, entity registry, systems |
| `Physics/` | Jolt Physics integration |
| `Scripting/Lua/` | Luau scripting with hot-reload |
| `Assets/` | Asset loading (GLTF, FBX, OBJ, images) |
| `TaskSystem/` | EnkiTS multi-threaded task scheduler |
| `Memory/` | RPMalloc integration, custom allocators |
| `Subsystems/` | Engine-level singletons (camera, input, etc.) |

### Reflection System

Classes/structs use `REFLECT()`, `GENERATED_BODY()`, `PROPERTY()`, and `FUNCTION(Script)` macros. The Reflector tool generates `.generated.h` files that enable automatic editor UI, serialization, and Lua binding generation. Always include `#include "MyClass.generated.h"` at the bottom of reflected headers.

### ECS

EnTT is the backing library. Key types:
- `CWorld` — game world, owns the entity registry
- `FEntityRegistry` — wraps `entt::registry`
- Components are reflected structs (`C` or `S` prefix)
- `FEntitySystemWrapper` — C++ systems; `FEntityScriptSystem` — Lua systems

### Object Handle System

**Never use raw pointers to `CObject`-derived types.** Use `TObjectHandle<T>` for safe generational references:
```cpp
TObjectHandle<CTransform> Handle;
if (Handle.IsValid()) { Handle->DoSomething(); }
```
Raw pointers are fine for non-`CObject` types, local parameters, and third-party APIs.

### Rendering

Forward+ (clustered) renderer. Key paths:
- `Renderer/RenderGraph/` — render graph abstraction
- `Renderer/API/` — Vulkan command buffers, descriptors
- `World/Scene/RenderScene/Forward/ForwardRenderScene.cpp` — main render scene

Vulkan is loaded via **Volk** (no direct function pointers). Memory via **VMA**.

## Naming Conventions

**Type prefixes are mandatory:**

| Prefix | Usage |
|--------|-------|
| `F` | Internal/non-reflected types (`FRenderer`, `FTexture`) |
| `C` | Reflected classes (`CTransform`, `CMeshRenderer`) |
| `S` | Reflected structs / data (`SVertex`, `SMaterial`) |
| `E` | Enumerations |
| `I` | Interfaces / abstract base classes |
| `T` | Template types (`TObjectHandle<T>`) |

- **PascalCase** for everything (classes, functions, variables, constants)
- **Tabs** for indentation, never spaces
- **Braces on new lines**
- No `m_Member` Hungarian notation, no snake_case

## Key Preprocessor Defines

- `LUMINA_DEBUG` / `LUMINA_DEVELOPMENT` / `LUMINA_SHIPPING`
- `WITH_EDITOR=1` (Editor platform) / `WITH_EDITOR=0` (Game platform)
- `LUMINA_RENDERER_VULKAN`
- Tracy profiler macros enabled in Debug and Development, disabled in Shipping

## Design Principles

- **Data-oriented design** over deep inheritance hierarchies — prefer flat data arrays and batch processing
- **Small, focused functions** — single responsibility, minimal parameters, early returns
- **Templates must use C++20 concepts** to constrain type parameters
- **Document with WHY** — explain reasoning, not just what the code does
