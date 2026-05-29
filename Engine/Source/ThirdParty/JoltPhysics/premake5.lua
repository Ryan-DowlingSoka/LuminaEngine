project "JoltPhysics"
	kind "StaticLib"
	warnings "off"
    -- AVX (not AVX2): must match the engine baseline in Workspace.lua. Jolt selects
    -- its SIMD path from these macros, and the path affects struct layout/alignment,
    -- so the lib and every engine TU that includes Jolt headers must agree. AVX2 here
    -- would also #UD-crash on CPUs without AVX2 (pre-Haswell / Atom / pre-Zen).
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