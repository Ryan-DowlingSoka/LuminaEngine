#include "ToolsMenuRegistry.h"

namespace Lumina
{
    FToolsMenuRegistry& FToolsMenuRegistry::Get()
    {
        static FToolsMenuRegistry Instance;
        return Instance;
    }

    void FToolsMenuRegistry::Register(FToolsMenuEntry Entry)
    {
        Entries.push_back(eastl::move(Entry));
    }
}
