project "Tracy"
	kind "SharedLib"
	language "C++"

	-- Tracy is a profiler for Debug/Development by default; Shipping builds
	-- normally have it disabled (macros become no-ops, library not linked), so
	-- drop the Shipping configuration to avoid producing a Tracy-Shipping.dll
	-- nobody uses. Keep it only when the user forces profiling into Shipping
	-- (BuildConfig.lua Tracy="on" / --tracy=on), where modules link Tracy there.
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