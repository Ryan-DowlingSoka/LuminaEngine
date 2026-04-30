#pragma once
#include <filesystem>

#include "EASTL/string.h"
#include "EASTL/vector.h"


namespace Lumina::Reflection
{
    class FReflectedProject;

    // One #include directive captured during the clang preprocessing pass.
    // Used by the post-parse validation step to enforce that a reflection-
    // bearing header includes its companion `<stem>.generated.h` and that
    // the include lives at the bottom of the include block.
    struct FIncludeRef
    {
        eastl::string Spelling;     // The literal text inside the angle/quote brackets.
        eastl::string Basename;     // Lowercased filename component, used for matching.
        uint32_t      LineNumber = 0;
    };

    class FReflectedHeader
    {
    public:

        FReflectedHeader(FReflectedProject* InProject, const eastl::string& Path);

        std::filesystem::file_time_type StartingFileTime;
        eastl::string                   FileName;
        eastl::string                   HeaderPath;
        FReflectedProject*              Project;

        // Captured via CXCursor_InclusionDirective during parsing. Order is
        // by source line, which is what the GeneratedHeaderNotLast check
        // relies on.
        eastl::vector<FIncludeRef>      Includes;

        // Set to true when the macro visitor sees REFLECT, GENERATED_BODY,
        // PROPERTY, or FUNCTION inside this header. Validation that depends
        // on the header being part of the reflection system is gated on it.
        bool                            bHasReflectionMacros = false;

        bool                            bDirty = false;
    };
}
