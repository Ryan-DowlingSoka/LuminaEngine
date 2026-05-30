#pragma once
#include <clang-c/Index.h>
#include "EASTL/string.h"
#include "Reflector/Types/PropertyFlags.h"


namespace Lumina
{
    struct FFieldInfo
    {
        CXType                              Type;
        // Cursor of the originating field; carried through sub-field creation so
        // diagnostics for inner properties still point at the user-facing source line.
        CXCursor                            OwningCursor = clang_getNullCursor();
        EPropertyTypeFlags                  Flags;
        EPropertyFlags                      PropertyFlags = EPropertyFlags::None;
        eastl::string                       Name;
        eastl::string                       TypeName;
        eastl::string                       RawFieldType;
    };
}
