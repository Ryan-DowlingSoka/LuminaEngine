LuminaModule({
    Name = "Editor",
    Kind = "SharedLib",
    Reflection = true,
    -- Editor PCH lives in Source/ so the `Source/**` files glob picks up
    -- EditorPCH.cpp as a regular TU. The header is uniquely named so the
    -- Reflector's libclang pass doesn't resolve `#include "pch.h"` to the
    -- editor PCH and loop into itself (Runtime also has a pch.h).
    PCH = { Header = "EditorPCH.h", Source = "Source/EditorPCH.cpp" },
    PublicIncludeDirs = { "." },
    ModuleDependencies = { "Runtime" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy", "Luau", "LuauAnalysis",
        -- Model-format parsers (moved out of Runtime). MeshOptimizer is needed
        -- directly by the GLTF importer; BasicUniversal by the texture cooker.
        "TinyOBJLoader", "OpenFBX", "FastGLTF", "MeshOptimizer", "BasicUniversal",
    },
    -- Editor never compiles for Game platform — sources reference WITH_EDITOR-only
    -- engine APIs that are stripped under WITH_EDITOR=0.
    RemovePlatforms = { "Game" },
})

-- NVIDIA Aftermath import lib (no DLL copy here — Runtime handles the copy).
LuminaOptions.LinkAftermath({ Copy = false })
