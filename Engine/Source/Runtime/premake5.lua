LuminaModule({
    Name = "Runtime",
    Kind = "SharedLib",
    RootFiles = true,
    PCH = { Header = "pch.h", Source = "pch.cpp" },
    Reflection = true,
    PublicIncludeDirs = { "." },
    PrivateDefines =
    {
        "GLFW_INCLUDE_NONE",
        "GLFW_STATIC",
        "LUMINA_RENDERER_VULKAN",
        "VK_NO_PROTOTYPES",
        "LUMINA_RPMALLOC",
        "LUMINA_HAS_RECAST",
    },
    -- The full set of third-party libraries the Runtime links and exposes
    -- through its public headers. Defined once in BuildScripts/ThirdParty so
    -- module dependents and external game projects resolve the same closure.
    -- Header-only entries (GLM, EnTT, spdlog, Vulkan, ...) contribute includes
    -- only; the registry's Link flags decide what actually links.
    -- Model-format parsers (tinyobjloader/OpenFBX/fastgltf) live in the Editor
    -- module; they don't ship in the Game runtime.
    Dependencies = LuminaThirdParty.RuntimePublicDeps,
    ExtraLinks =
    {
        "slang",
        "slang-compiler",
    },
    LibDirs =
    {
        LuminaConfig.EnginePath("External/SLang/lib"),
    },
    FatalWarnings = { "4456", "4457", "4458", "4238" },
    ExtraFiles = { },
})

-- NVIDIA Aftermath import lib + runtime DLL copy, scoped to the configurations
-- where Aftermath is enabled (LuminaOptions / BuildConfig.lua). Runtime owns the
-- DLL copy since it always builds into the same Binaries dir as the executable;
-- the copy runs postbuild so the target dir is guaranteed to exist.
LuminaOptions.LinkAftermath({ Copy = true })
