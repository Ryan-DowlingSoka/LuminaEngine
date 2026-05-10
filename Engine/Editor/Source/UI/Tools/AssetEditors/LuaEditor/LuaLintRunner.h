#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // Single Luau lint warning, mapped from Luau::LintWarning into engine types
    // so callers don't pull in Luau Analysis headers.
    struct FLuaLintWarning
    {
        // 1-based source line of the warning. Matches Luau::Location::begin.line + 1.
        int     Line   = 0;
        // 1-based source column of the warning's start. Matches Luau::Location::begin.column + 1.
        int     Column = 0;
        // Luau lint code (LintWarning::Code enum value). 0 means unknown.
        int     Code   = 0;
        // Short human-readable lint family name (e.g. "TableOperations", "ForRange").
        FString Name;
        // Full warning message produced by Luau.
        FString Message;
    };

    // Runs Luau's Analysis linter against the given source. Returns true if the
    // source parsed cleanly (warnings list is then populated; may be empty).
    // Returns false on parse failure - in that case the FCompileDiagnostic side
    // of the pipeline is the right place to surface the error, so we don't try
    // to emit lint warnings against a syntactically broken document.
    //
    // VirtualPath is used as the chunk name in error messages.
    //
    // Note: this skips type-info-dependent lint codes (UnknownGlobal/UnknownType/
    // DeprecatedApi) since we don't run the full type checker. Everything else
    // - perf hints, dead code, suspicious patterns - is enabled.
    bool RunLuauLint(FStringView Source, FStringView VirtualPath, TVector<FLuaLintWarning>& Out);
}
