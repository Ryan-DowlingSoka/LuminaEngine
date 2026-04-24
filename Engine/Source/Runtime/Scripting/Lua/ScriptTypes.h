#pragma once

#include "Reference.h"
#include "ScriptExports.h"
#include "Containers/Name.h"
#include "Containers/String.h"

namespace Lumina::Lua
{
    struct FScript
    {
        FName                           Name;
        FString                         Path;
        FRef                            Reference;
        FRef                            Environment;
        FRef                            Thread;

        /** Schema parsed from the `type Exports = {...}` alias; empty if none/unrecognised. */
        FScriptExportSchema             ExportsSchema;

        /** Default values read from the runtime `Exports` table right after script execution. */
        TVector<FScriptPropertyEntry>   ExportDefaults;

        bool                            bDirty = false;
    };
}
