project "JoltPhysics"
	kind "StaticLib"
	warnings "off"
    vectorextensions "AVX2"

	defines
	{
		"__AVX2__",
	}

	files
	{
		"**.h",
		"**.cpp",
		"**.lua",
	}

	includedirs
	{
		".",
	}