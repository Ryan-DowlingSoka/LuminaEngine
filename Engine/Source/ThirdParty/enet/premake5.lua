project "ENet"
	kind "StaticLib"
	language "C"
	warnings "off"

	includedirs { "include" }

	files
	{
		"include/enet/**.h",
		"callbacks.c",
		"compress.c",
		"host.c",
		"list.c",
		"packet.c",
		"peer.c",
		"protocol.c",
		"premake5.lua",
	}

	filter "system:windows"
		systemversion "latest"
		files { "win32.c" }
		defines { "_CRT_SECURE_NO_WARNINGS", "_WINSOCK_DEPRECATED_NO_WARNINGS" }

	filter "system:not windows"
		files { "unix.c" }
		defines { "HAS_SOCKLEN_T" }

	filter {}
