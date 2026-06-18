#pragma once
#include "ReflectedHeader.h"
#include "StringHash.h"

#include "EASTL/hash_map.h"
#include "EASTL/unique_ptr.h"
#include "eastl/vector.h"

namespace Lumina::Reflection
{
    class FReflectedWorkspace;

    class FReflectedProject
    {
    public:
        
        FReflectedProject& operator =(const FReflectedProject&) = delete;
        FReflectedProject(const FReflectedProject&) = delete;

        FReflectedProject(FReflectedWorkspace* InWorkspace);
        
        eastl::string                                                       Name;
        eastl::string                                                       Path;
        // Override destination for this project's generated C# bindings (.generated.cs). Empty = the default
        // Intermediates/CSharpBindings/<Name> (engine modules → LuminaSharp.dll). A plugin/game module sets
        // this to its <root>/Scripts/Generated so the per-plugin script gather compiles the bindings into the
        // plugin's OWN assembly, not LuminaSharp.dll.
        eastl::string                                                       CSharpBindingsDir;
        FReflectedWorkspace*                                                Workspace;
        eastl::hash_map<FStringHash, eastl::unique_ptr<FReflectedHeader>>   Headers;
        eastl::vector<eastl::string>                                        IncludeDirs;
    };
}
