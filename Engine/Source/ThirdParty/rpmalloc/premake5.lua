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

	-- rpmalloc_global_statistics() reports zero for mapped/peak/huge unless this is
	-- set. The Memory profiler uses it to split "untracked" RSS into allocator-retained
	-- (caches + fragmentation) vs external (driver / Luau / CRT). Atomic counters on
	-- alloc/free; kept out of Shipping.
	filter "configurations:not Shipping"
		defines { "ENABLE_STATISTICS=1" }
	filter {}