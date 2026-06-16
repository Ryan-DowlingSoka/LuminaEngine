#include "ReflectedFunction.h"

namespace Lumina
{
    void FReflectedFunction::GenerateMetadata(const eastl::string& InMetadata)
    {
        if (InMetadata.empty())
        {
            return;
        }

        FMetadataParser Parser(InMetadata);
        Metadata = eastl::move(Parser.Metadata);
    }
}
