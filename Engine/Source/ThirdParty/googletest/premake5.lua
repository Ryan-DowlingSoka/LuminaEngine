project "GoogleTest"
	kind "StaticLib"
	warnings "off"

	files
	{
		"include/**.h",
		"src/**.cc",
		"**.lua",
	}

	includedirs
	{
		".",
		"include"
	}