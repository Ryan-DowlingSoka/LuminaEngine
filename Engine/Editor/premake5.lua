LuminaModule({
    Name = "Editor",
    Kind = "SharedLib",
    Reflection = true,
    PublicIncludeDirs = { "." },
    ModuleDependencies = { "Runtime" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy", "Luau",
    },
    ExtraLinks = { "GFSDK_Aftermath_Lib" },
    LibDirs =
    {
        LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib"),
    },
    -- Editor never compiles for Game platform — sources reference WITH_EDITOR-only
    -- engine APIs that are stripped under WITH_EDITOR=0.
    RemovePlatforms = { "Game" },
})
