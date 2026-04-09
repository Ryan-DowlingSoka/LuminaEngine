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
    },
    Dependencies =
    {
        "MiniAudio", "GLFW", "ImGui", "EA", "Tracy", "Luau", "EnkiTS",
        "JoltPhysics", "RPMalloc", "XXHash", "Volk", "VKBootstrap",
        "TinyOBJLoader", "MeshOptimizer", "SPIRV-Reflect", "FastGLTF",
        "OpenFBX", "BasicUniversal",
    },
    ExtraLinks =
    {
        "slang", "slang-compiler",
        "GFSDK_Aftermath_Lib.lib", "GFSDK_Aftermath_Lib.x64.dll",
    },
    LibDirs =
    {
        LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib"),
        LuminaConfig.EnginePath("External/SLang/lib"),
    },
    FatalWarnings = { "4456", "4457", "4458", "4238" },
    PrebuildCommands =
    {
        LuminaConfig.CopyFile(LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib/GFSDK_Aftermath_Lib.x64.dll"), LuminaConfig.GetTargetDirectory()),
        LuminaConfig.CopyFile(LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib/GFSDK_Aftermath_Lib.lib"), LuminaConfig.GetTargetDirectory()),
    },
    ExtraFiles = { },
})
