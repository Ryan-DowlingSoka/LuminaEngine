project "EA"
	kind "StaticLib"
	language "C++"

	files
	{
		"**.h",
		"**.cpp",
		"**.lua",
	}

	includedirs
	{
		"EABase",
		"EASTL",
		"EABase/include/Common",
		"EASTL/include/",
	}
