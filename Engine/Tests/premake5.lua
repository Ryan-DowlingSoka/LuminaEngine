LuminaModule({
    Name = "Tests",
    Kind = "ConsoleApp",
    ModuleDependencies = { "Runtime", },
    EditorModuleDependencies = { "Editor" },
    Dependencies =
    {
        "GoogleTest", "ImGui", "RPMalloc", "EA", "Tracy", "Luau", "EnTT",
    },
    PrivateIncludeDirs = 
    { 
        ".",
        LuminaConfig.EnginePath("Engine/Source/ThirdParty/GoogleTest/include"),
    },
})
