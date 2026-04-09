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
