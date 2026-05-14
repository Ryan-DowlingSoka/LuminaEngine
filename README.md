<div align="center">

<img src="https://github.com/user-attachments/assets/552b8ca0-ebca-4876-9c6a-df38c468d41e" width="120"/>

# Lumina Game Engine
**A modern, high-performance game engine built with Vulkan**

*"Everything around you that you call life, was made up by people that were no smarter than you"* - Steve Jobs

[![License](https://img.shields.io/github/license/mrdrelliot/lumina)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-blue)](https://github.com/mrdrelliot/lumina)
[![C++](https://img.shields.io/badge/C++-23-blue)](https://github.com/mrdrelliot/lumina)
[![Vulkan](https://img.shields.io/badge/Vulkan-renderer-red)](https://www.vulkan.org/)
[![Discord](https://img.shields.io/discord/1193738186892005387?label=Discord&logo=discord)](https://discord.gg/xQSB7CRzQE)

[Blog](https://www.dr-elliot.com) • [Discord](https://discord.gg/xQSB7CRzQE) • [Documentation](#-documentation)

</div>

<div align="center">
  <img width="800" height="600" alt="image" src="https://github.com/user-attachments/assets/ba4fc531-362c-4b4f-bbec-261bb1e79652" />
  <img width="800" height="600" alt="image" src="https://github.com/user-attachments/assets/5df3dee0-71fe-4439-b851-5e22ff2b23cc" />
---

<details>
  <summary><b>Show more images</b></summary>

  <div align="center">
    <img width="800" height="600" alt="image" src="https://github.com/user-attachments/assets/086a1e5e-4d8d-4c98-be81-bc77287fba39" />
    <img width="800" height="800" alt="image" src="https://github.com/user-attachments/assets/66d739ca-f257-4350-b2f0-2e10d66f5591" />
    <img width="800" height="800" alt="image" src="https://github.com/user-attachments/assets/68e86207-20f2-4200-8649-257738b2855f" />
    <img width="800" height="800" alt="image" src="https://github.com/user-attachments/assets/a4c7c497-d975-47d2-a695-5342e11e8f44" />
    <img width="800" height="800" alt="image" src="https://github.com/user-attachments/assets/5343eff8-545a-47ca-a858-b2aa4f1fef71" />
    </div>
</details>
</div>

---

https://github.com/user-attachments/assets/3d797479-fc47-4b8f-baf4-87315709d0c2


## About
Lumina is a modern C++ game engine designed for learning and experimentation with real-world engine architecture. Built from the ground up with Vulkan, it demonstrates professional engine design patterns including reflection systems, ECS architecture, and advanced rendering techniques.

**Perfect for:**
- Learning modern game engine architecture
- Experimenting with Vulkan rendering techniques
- Building prototypes with a clean, modular codebase
- Understanding how engines like Unreal and Godot work under the hood

Contributions to Lumina are recognized in several ways, including Steam keys for popular games and public acknowledgment in our Discord community. Lumina improves through the work of motivated, like-minded contributors who help push the engine forward.

> [!CAUTION]
> Lumina is an educational project in active development. APIs may change, and some features are experimental. If you encounter build issues, please reach out on [Discord](https://discord.gg/xQSB7CRzQE) for assistance.

---

## Key Features

### **Advanced Rendering**
- **Vulkan-powered renderer** with automatic resource tracking and barrier placement
- **Forward+ rendering pipeline** with clustered lighting for efficient multi-light scenes
- **PBR materials using a material graph compiled into GLSL.**

### **Modern Architecture**
- **Entity Component System (ECS)** using EnTT for high-performance gameplay code
- **Reflection system** for automatic serialization and editor integration
- **Modular design** with clean separation of concerns

### **Professional Editor**
- **ImGui-based editor** with real-time scene manipulation
- **Visual hierarchy** for easy entity management
- **Component inspector** with automatic UI generation via reflection

### **Performance First**
- **Multi-threaded task system** with EnkiTS
- **Custom memory allocators** using RPMalloc for optimal performance
- **Built-in profiling** with Tracy integration

### **Lua Scripting**

- Full ECS access from Lua - Create systems, query entities, modify components
- Hot-reloadable scripts - Iterate on gameplay without recompiling
- Automatic binding generation - C++ components instantly available in Lua through reflection
- Performance profiling - Built-in Lua script profiling with Tracy

---

## Gallery

*New Images Coming Soon*

---

## Quick Start

### What You Need
- **Windows 10 (1803+) or Windows 11** (64-bit)
- **Visual Studio 2022** with MSVC v143 toolset (17.8+)

> [!NOTE]
> [JetBrains Rider](https://www.jetbrains.com/rider/) is the recommended IDE for Lumina development (not required).

### Installation Steps

1. **Clone the repository**
   ```bash
   git clone https://github.com/mrdrelliot/luminaengine
   cd LuminaEngine
   ```

2. **Run setup**
   ```
   Setup.bat
   ```
   - Downloads and extracts all external dependencies, persists `LUMINA_DIR`, configures git hooks, and generates `Lumina.sln`
   - No Python required — uses `curl.exe` and `tar.exe` bundled with Windows
   - If the download fails, manually grab [External.zip](https://www.dropbox.com/scl/fi/mzad6ruqibzsmam30npju/External.zip?rlkey=egj0adfoytpjydnhbs53qd3lh&st=pw81jqsw&dl=0) and extract it into the `LuminaEngine/` folder, then run `GenerateProjectFiles.bat`

3. **Open the solution**
   - Open `Lumina.sln` in Visual Studio

4. **Build and run Lumina**
   - Set `Lumina` as the startup project
   - Pick `Development` or `Debug` (Debug is far slower but enables full debugger functionality)
   - Two platforms are exposed: `Editor` (default, includes editor tooling) and `Game` (runtime only, no editor)
   - Press F5 or **Build -> Run**

5. **Start developing**
   - Open the `Sandbox` project to experiment
   - Or copy `Templates/Blank/` to create a new project (run its `GenerateProject.bat` to produce a solution)

> [!TIP]
> - **Missing v143 toolset?** Install it via Visual Studio Installer -> Individual Components -> MSVC v143 Build Tools.
> - **"Cannot find .generated.h" error?** Build again — Visual Studio sometimes needs a second pass to pick up generated files.
> - **C1076 compiler limit reached?** Retry the build; this is a known intermittent issue with a font file.
> - **"Application control policy blocked this file"?** Disable Windows 11 *Smart App Control*.
> - **Build fails?** [Submit an issue](https://github.com/mrdrelliot/LuminaEngine/issues) or reach out on Discord.

> [!NOTE]
> `Setup.bat` persists `LUMINA_DIR` automatically (via `setx`). If you need to set it manually:
> ```bash
> setx LUMINA_DIR "C:\path\to\lumina"
> ```

> [!CAUTION]
> After pulling or merging, delete `Binaries/` and `Intermediates/` and run `GenerateProjectFiles.bat` to regenerate the solution.

---

## Supported Asset Formats

- **GLTF**
- **FBX**
- **GLB**
- **OBJ**
- **PNG**
- **JPG**

### Free Asset Resources

- [Khronos GLTF Samples](https://github.com/KhronosGroup/glTF-Sample-Assets)
- [Kenney 3D Assets](https://kenney.nl/assets?q=3d)
- [Flightradar24 Models](https://github.com/Flightradar24/fr24-3d-models)

---

### Third Party Dependencies (Alphabetical)

* **basis_universal** - GPU texture compression supporting transcoding to BC7/ETC/ASTC at runtime
* **ConcurrentQueue** - Industrial-strength lock-free queue supporting multiple producers and consumers
* **EASTL** - Electronic Arts Standard Template Library optimized for game development with custom allocators
* **EnkiTS** - Lightweight task scheduler for parallel-for, task sets, and dependency graphs across threads
* **EnTT** - Fast entity component system with sparse-set storage and signal/delegate support
* **FastGLTF** - High-performance GLTF 2.0 parser with complete specification support
* **GLFW** - Multi-platform window and input library for OpenGL, OpenGL ES, and Vulkan
* **GLM** - Header-only C++ mathematics library with GLSL-compatible syntax
* **GoogleTest** - C++ testing framework providing assertions, fixtures, and test discovery
* **ImGui** - Immediate-mode GUI for rapid tool development with minimal dependencies
* **JoltPhysics** - High-performance multi-threaded physics engine with continuous collision detection
* **Luau** - Efficient, optionally-typed scripting language derived from Lua
* **MeshOptimizer** - Mesh optimization for vertex cache, overdraw, and vertex/index buffer compression
* **Miniaudio** - Single-file audio playback and capture library
* **Nlohmann JSON** - Modern JSON library with intuitive syntax and STL compatibility
* **NVIDIA Aftermath** - GPU crash debugging and post-mortem dump analysis
* **OpenFBX** - Lightweight FBX loader for geometry, skeletons, and animation (no Autodesk SDK)
* **RenderDoc** - Graphics debugger integration for frame capture and analysis
* **RPMalloc** - Lock-free thread-caching memory allocator
* **Slang** - Modern shader language and compiler with SPIR-V / HLSL output
* **SPDLog** - Fast C++ logging library with async mode and multiple sinks
* **stb_image** - Single-header image loading library for common formats
* **TinyObjLoader** - Lightweight OBJ parser with MTL material support
* **Tracy** - Real-time frame profiler with sampling, GPU zones, and lock contention tracking
* **vk-bootstrap** - Vulkan initialization helper that reduces boilerplate
* **Volk** - Vulkan meta-loader for runtime function loading
* **Vulkan** - Low-level graphics API providing explicit GPU control
* **VulkanMemoryAllocator** - Memory management library for Vulkan with defragmentation
* **xxHash** - Extremely fast non-cryptographic hash algorithm



---

## Documentation

### Coding Standards

Lumina follows a consistent naming convention:

| Prefix | Usage | Example |
|--------|-------|---------|
| `F` | Internal engine types (non-reflected) | `FRenderer`, `FTexture` |
| `C` | Reflected classes | `CTransform`, `CMeshRenderer` |
| `S` | Reflected structs | `SVertex`, `SMaterial` |

**General Rules:**
- PascalCase for all identifiers
- Tabs for indentation
- Braces on new lines
- Descriptive variable names

See [CONTRIBUTING.md](CONTRIBUTING.md) for complete guidelines.

---

## Contributing

Contributions are welcome! Whether it's bug fixes, features, or documentation improvements.

### How to Contribute
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes following the [coding standards](CONTRIBUTING.md)
4. Add tests if applicable
5. Commit with clear messages (`git commit -m 'Add amazing feature'`)
6. Push to your branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

**Requirements:**
- Clean, well-documented code
- Follow existing architecture patterns
- Include tests where appropriate
- Update documentation as needed

---

## Acknowledgments

Lumina is inspired by and learns from these excellent open-source engines:

- [**Spartan Engine**](https://github.com/PanosK92/SpartanEngine) - Feature-rich Vulkan engine
- [**Kohi Game Engine**](https://kohiengine.com/) - Educational engine series
- [**Lumix Game Engine**](https://github.com/nem0/LumixEngine) - Fully working indie engine.
- [**ezEngine**](https://ezengine.net/) - Professional open-source engine
- [**GoDot**](https://godotengine.org/) - AAA quality open source C++ game engine.
- [**Unreal Engine**](https://www.unrealengine.com/) - Does it need an introduction?

Special thanks to the entire game engine development community for sharing knowledge and resources.

---

## License

Lumina is licensed under the [Apache 2.0 License](LICENSE).

```
Copyright 2024 Dr. Elliot

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

---

## Connect

- **Blog**: [dr-elliot.com](https://www.dr-elliot.com)
- **Discord**: [Join our community](https://discord.gg/xQSB7CRzQE)
- **GitHub**: [mrdrelliot/lumina](https://github.com/mrdrelliot/lumina)

---

<div align="center">

**Made with ❤️ for the game development community**

⭐ Star this repo if you find it useful!

</div>
