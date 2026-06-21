project "Tracy"
	kind "SharedLib"
	language "C++"

	-- Drop Shipping unless profiling is forced on for it, else we ship an unused Tracy-Shipping.dll.
	if not LuminaOptions.IsActive("Tracy", "Shipping") then
		removeconfigurations { "Shipping" }
	end

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