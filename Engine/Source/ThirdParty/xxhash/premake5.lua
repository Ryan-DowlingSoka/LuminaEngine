project "XXHash"
	kind "StaticLib"
	warnings "off"

	files
	{
		"**.h",
		"**.c",
		"**.lua",
	}

	filter "configurations:Debug"
		editandcontinue "Off"
	filter {}