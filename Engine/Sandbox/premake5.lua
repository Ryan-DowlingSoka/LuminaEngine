project "Sandbox"
	kind "SharedLib"
	rtti "off"
	enablereflection "On" --@TODO Cannot have a project with no reflected files.

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
		"Runtime",
		"ImGui",
		"RPMalloc",
    	"EA",
    	"EnkiTS",
		"Tracy",
        "Luau",
	}
	 
	files
	{
		"Source/**.h",
		"Source/**.cpp",
		--LuminaConfig.GetReflectionFiles(),
		"**.lua",
		"**.lproject",
		"**.json",
	}

	forceincludes
	{
		"SandboxAPI.h"
	}

	includedirs
	{
		"Source",
	    LuminaConfig.GetPublicIncludeDirectories(),
	}
	 