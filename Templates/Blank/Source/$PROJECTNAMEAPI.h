#pragma once

/**
 * $PROJECTNAME API Export/Import Macros
 *
 * Force-included in every translation unit of this module.
 * Including ModuleAPI.h here ensures RUNTIME_API, EDITOR_API, etc. are defined
 * for all engine headers without needing a separate force-include entry.
 */
#include "ModuleAPI.h"

#ifndef $PROJECTNAMEUPPER_API
    #ifdef $PROJECTNAMEUPPER_EXPORTS
        #define $PROJECTNAMEUPPER_API DLL_EXPORT
    #else
        #define $PROJECTNAMEUPPER_API DLL_IMPORT
    #endif
#endif
