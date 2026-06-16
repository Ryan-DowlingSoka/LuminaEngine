LuminaModule({
    Name = "Sandbox",
    Kind = "SharedLib",
    Reflection = true,
    ModuleDependencies = { "Runtime" },
    EditorModuleDependencies = { "Editor" },
    Dependencies =
    {
        "ImGui", "RPMalloc", "EA", "Tracy",
    },
    ExtraFiles = { "**.lproject", "**.json" },
})
