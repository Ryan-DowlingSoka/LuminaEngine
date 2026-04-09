LuminaModule({
    Name = "Sandbox",
    Kind = "SharedLib",
    Reflection = true,
    ModuleDependencies = { "Runtime" },
    EditorModuleDependencies = { "Editor" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "EnkiTS", "Tracy", "Luau",
    },
    LibDirs =
    {
        LuminaConfig.EnginePath("Engine/Source/ThirdParty/lua"),
    },
    ExtraFiles = { "**.lproject", "**.json" },
})
