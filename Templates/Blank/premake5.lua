local LuminaDir = os.getenv("LUMINA_DIR")
print(LuminaDir)

include (path.join(LuminaDir, "BuildScripts/Dependencies"))
include (path.join(LuminaDir, "BuildScripts/Actions/Reflection"))

workspace "$PROJECTNAME"
	language "C++"
	cppdialect "C++latest"
    warnings "Default"
    targetdir (LuminaConfig.GetTargetDirectory())
    objdir (LuminaConfig.GetObjDirectory())
    enableunitybuild "Off"
    fastuptodate "On"
    multiprocessorcompile "On"
	startproject "$PROJECTNAME"

	configurations 
    { 
        "Debug",
        "Development",
        "Shipping",
    }

	platforms
    {
        "Editor",
        "Game",
    }


project "$PROJECTNAME"
	kind "SharedLib"
	rtti "off"
	enablereflection "On"

	libdirs
	{
		LuminaConfig.EnginePath("Engine/Source/ThirdParty/lua"),
	}

	filter "platforms:Editor"
		links "Editor"
		includedirs
		{
			LuminaConfig.EnginePath("Engine/Editor/Source")
		}
	filter {}

	links
	{
        "MiniAudio", "GLFW", "ImGui", "EA", "Tracy", "Luau", "EnkiTS",
        "JoltPhysics", "RPMalloc", "XXHash", "Volk", "VKBootstrap",
        "TinyOBJLoader", "MeshOptimizer", "SPIRV-Reflect", "FastGLTF",
        "OpenFBX", "BasicUniversal",
	}
	 
	files
	{
		"Source/**.h",
		"Source/**.cpp",
		LuminaConfig.GetReflectionFiles(),
		"**.lua",
		"**.lproject",
		"**.json",
	}

	forceincludes
	{
		"$PROJECTNAMEAPI.h"
	}

	includedirs
	{
		"Source",
	    LuminaConfig.GetPublicIncludeDirectories(),
	}
	 

