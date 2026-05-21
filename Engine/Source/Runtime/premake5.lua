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
        "GFSDK_Aftermath_Lib.lib", 
        "GFSDK_Aftermath_Lib.x64.dll",
    },
    LibDirs =
    {
        LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib"),
        LuminaConfig.EnginePath("External/SLang/lib"),
    },
    FatalWarnings = { "4456", "4457", "4458", "4238" },
    PrebuildCommands =
    {
        -- Same locked-DLL caveat as SLang: when the editor triggers a Game
        -- rebuild it may have these DLLs loaded; tolerate the failure since
        -- the existing copy on disk is the version we'd be writing anyway.
        LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib/GFSDK_Aftermath_Lib.x64.dll"), LuminaConfig.GetTargetDirectory()),
        LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib/GFSDK_Aftermath_Lib.lib"), LuminaConfig.GetTargetDirectory()),
    },
    ExtraFiles = { },
})
