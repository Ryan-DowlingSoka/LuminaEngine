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
    -- Public third-party deps; shared with dependents and external game projects (model-format parsers stay in Editor).
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

-- Runtime owns the Aftermath DLL copy since it builds into the executable's Binaries dir.
LuminaOptions.LinkAftermath({ Copy = true })

-- /GT (fiber-safe TLS) required or the scheduler reads stale TLS after fiber migration and segfaults. See JobScheduler.cpp.
filter { "files:**/JobScheduler.cpp" }
    buildoptions { "/GT" }
filter {}
