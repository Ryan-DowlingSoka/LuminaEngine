project "JoltPhysics"
	kind "StaticLib"
	warnings "off"
    -- AVX (not AVX2): must match the engine baseline in Workspace.lua (affects Jolt struct layout/alignment); don't bump one alone.
    vectorextensions "AVX"

	defines
	{
		"__AVX__",
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