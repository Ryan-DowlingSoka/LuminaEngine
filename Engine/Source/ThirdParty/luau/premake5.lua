project "Luau"
	kind "SharedLib"
	warnings "off"

	defines
	{
		"LUA_BUILD_AS_DLL",
		"LUA_LIB",
	}

	files
	{
		"include/**.h",
		"Source/Ast/**.cpp",
		"Source/Common/**.cpp",
		"Source/Compiler/**.cpp",
		"Source/Config/**.cpp",
		"Source/VM/**.cpp",
		"**.lua",
	}

	includedirs
	{
		"include"
	}


-- Luau type-checker / linter (Analysis library). Editor-only because the
-- runtime never needs to compile-check or lint scripts; it just runs them.
-- Built as a static lib that gets statically linked into Editor.dll.
--
-- Why we re-bundle Ast/Common/Compiler/Config sources here (they're already
-- compiled into Luau.dll): Luau only dllexports the C API (LUA_API in
-- luaconf.h). The Ast/Compiler C++ classes - AstNameTable, Parser,
-- BytecodeBuilder, format(), Location::contains(), etc. - have no export
-- attribute, so the Luau.dll import lib doesn't surface them. Editor's
-- LuauAnalysis link can't resolve them through Luau.dll. Compiling those
-- source trees into LuauAnalysis.lib gives Editor.dll its own static copy
-- of the AST machinery; the linker dead-strips the unused bits.
--
-- Game configs still build it (the work is small) but no consumer references
-- it there.
project "LuauAnalysis"
	kind "StaticLib"
	warnings "off"

	defines
	{
		-- Headers gate LUA_API on LUA_BUILD_AS_DLL; we want imported (not
		-- re-defined) lua.h symbols since UserDefinedTypeFunction.cpp /
		-- TypeFunctionRuntime.cpp call into Luau.dll's exported VM API.
		"LUA_BUILD_AS_DLL",
	}

	files
	{
		"include/Luau/**.h",
		"Source/Analysis/**.cpp",
		"Source/Analysis/**.h",
		"Source/Ast/**.cpp",
		"Source/Common/**.cpp",
		"Source/Compiler/**.cpp",
		"Source/Config/**.cpp",
		"**.lua",
	}

	includedirs
	{
		"include",
		-- AutocompleteCore.cpp / Linter.cpp / etc. include sibling headers
		-- inside Source/Analysis/ without the Luau/ prefix.
		"Source/Analysis",
	}