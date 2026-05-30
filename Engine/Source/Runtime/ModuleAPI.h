#pragma once

// Force-included in every TU; resolves DLL export/import macros from the per-module *_EXPORTS define.
// To add a module, add a block below.

#ifndef REFLECTION_PARSER

// Monolithic builds link statically, so define the macros empty before the per-module blocks skip them.
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
