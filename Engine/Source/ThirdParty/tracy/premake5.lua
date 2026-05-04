project "Tracy"
	kind "SharedLib"
	language "C++"

	-- Tracy is a profiler for Debug/Development only. Shipping builds have
	-- TRACY_ENABLE removed so the profiling macros expand to no-ops, and the
	-- Tracy library is not linked. Drop the Shipping configuration entirely
	-- so we don't waste time producing a Tracy-Shipping.dll nobody uses.
	removeconfigurations { "Shipping" }

	defines
	{
		"TRACY_EXPORTS",
		"TRACY_ALLOW_SHADOW_WARNING",
	}

	files
	{
		"public/TracyClient.cpp",
		"**.lua",
	}
	
	includedirs
	{
		"public",
	}