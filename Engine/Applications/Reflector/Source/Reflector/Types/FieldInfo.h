#pragma once
#include <clang-c/Index.h>
#include "EASTL/string.h"
#include "Reflector/Types/PropertyFlags.h"


namespace Lumina
{
    struct FFieldInfo
    {
        CXType                              Type;
        // Cursor of the originating field declaration. Carried through sub-field
        // creation so diagnostics emitted while building inner properties (array
        // elements, optional payloads) still point at the user-facing source line.
        CXCursor                            OwningCursor = clang_getNullCursor();
        EPropertyTypeFlags                  Flags;
        EPropertyFlags                      PropertyFlags = EPropertyFlags::None;
        eastl::string                       Name;
        eastl::string                       TypeName;
        eastl::string                       RawFieldType;
    };
}
