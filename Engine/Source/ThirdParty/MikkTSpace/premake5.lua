project "MikkTSpace"
	kind "StaticLib"
	language "C"
	warnings "off"

	files
	{
		"src/**.c",
		"src/**.h",
		"**.lua",
	}

	includedirs
	{
		"src/",
	}
