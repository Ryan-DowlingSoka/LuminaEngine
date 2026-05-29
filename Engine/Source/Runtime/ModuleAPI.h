#pragma once

/**
 * Universal Module API Header
 *
 * This header is force-included in every translation unit across all modules.
 * It handles DLL export/import macros automatically using the workspace-level
 * {PROJECTNAME}_EXPORTS define that Premake sets for SharedLib projects.
 *
 * To add a new module, add a corresponding block below.
 */

#ifndef REFLECTION_PARSER

// Monolithic builds (LUMINA_MONOLITHIC, set in Shipping config by
// Workspace.lua) link every module statically into one image, so the
// dllexport/dllimport dance is not just unnecessary -- it's wrong.
// Define the macros as empty BEFORE the per-module blocks so each
// module's #ifndef-guarded block silently skips.
#ifdef LUMINA_MONOLITHIC
	#define RUNTIME_API
	#define EDITOR_API
	#define SANDBOX_API
#endif

// Runtime
#ifndef RUNTIME_API
	#ifdef RUNTIME_EXPORTS
		#define RUNTIME_API DLL_EXPORT
	#else
		#define RUNTIME_API DLL_IMPORT
	#endif
#endif

// Editor
#ifndef EDITOR_API
	#ifdef EDITOR_EXPORTS
		#define EDITOR_API DLL_EXPORT
	#else
		#define EDITOR_API DLL_IMPORT
	#endif
#endif

// Sandbox
#ifndef SANDBOX_API
	#ifdef SANDBOX_EXPORTS
		#define SANDBOX_API DLL_EXPORT
	#else
		#define SANDBOX_API DLL_IMPORT
	#endif
#endif

#endif // REFLECTION_PARSER
