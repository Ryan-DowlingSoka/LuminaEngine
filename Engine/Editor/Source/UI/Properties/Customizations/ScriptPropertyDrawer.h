#pragma once

#include "Scripting/ScriptExports.h"

namespace Lumina::ScriptPropertyDrawer
{
    // Emits one or more rows (assumes an active 2-column ImGui table) for a script-export value and its
    // children: fully recursive over scalars, arrays, and nested structs, all meta-driven. Used by the
    // C# script-component inspector, which drives the FScriptExportType / FScriptPropertyValue model.
    // bOutRemove, when non-null, draws a remove button (for array elements).
    void DrawValueRows(const Scripting::FScriptExportType& Type, Scripting::FScriptPropertyValue& Value,
                       const char* Label, const Scripting::FScriptExportMeta& Meta, bool& bChanged, bool* bOutRemove = nullptr);
}
