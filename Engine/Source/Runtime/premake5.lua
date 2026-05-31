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
    -- Third-party closure shared with module dependents and external game projects; model-format parsers live in Editor, not the Game runtime.
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

-- Aftermath import lib + DLL copy; Runtime owns the copy since it builds into the executable's Binaries dir.
LuminaOptions.LinkAftermath({ Copy = true })

-- The fiber job scheduler reads thread_local state (worker index, current fiber, scheduler fiber)
-- across SwitchToFiber, and fibers migrate between OS threads on reuse. /GT (fiber-safe TLS) stops the
-- optimizer from caching a stale TLS block pointer across a switch — without it the scheduler reads
-- another thread's TLS after a migration and jumps through a garbage job pointer. See JobScheduler.cpp.
filter { "files:**/JobScheduler.cpp" }
    buildoptions { "/GT" }
filter {}
