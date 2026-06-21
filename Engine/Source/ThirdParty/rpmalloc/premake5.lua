project "RPMalloc"
	kind "StaticLib"
	warnings "off"
    

	files
	{
		"**.h",
		"**.c",
		"**.lua",
	}

	includedirs
	{
		".",
	}

	-- Required for rpmalloc_global_statistics() to report mapped/peak/huge (Memory profiler reads it); kept out of Shipping.
	filter "configurations:not Shipping"
		defines { "ENABLE_STATISTICS=1" }
	filter {}