project "Recast"
	kind "StaticLib"
	warnings "off"

	files
	{
		"Recast/Source/**.cpp",
		"Recast/Include/**.h",
		"Detour/Source/**.cpp",
		"Detour/Include/**.h",
	}

	includedirs
	{
		"Recast/Include",
		"Detour/Include",
	}
