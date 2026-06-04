#pragma once

#include "Containers/String.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    // Editor-side model of the script annotation DSL (--@export / --@rpc / --@replicated). The runtime
    // parser (Lumina::Lua::ScanScriptAnnotations) stays the behavioral source of truth; this mirrors its
    // grammar so the Lua editor can offer autocomplete, hover docs, and outline entries that make the
    // comment directives feel like first-class language constructs.

    // One directive name or argument token, with the documentation surfaced on hover / completion.
    struct FLuaAnnotationToken
    {
        const char* Name        = nullptr;
        const char* Detail      = nullptr;  // short dim hint shown right-aligned in the popup
        const char* Doc         = nullptr;  // tooltip body
        bool        bTakesValue = false;    // true => completion inserts "Name="; false => bare flag
    };

    // Non-owning view over a static token table.
    struct FLuaAnnotationTokenList
    {
        const FLuaAnnotationToken* Data  = nullptr;
        int                        Count = 0;
        const FLuaAnnotationToken* begin() const { return Data; }
        const FLuaAnnotationToken* end()   const { return Data + Count; }
    };

    // What the cursor is completing inside a `--@...` comment.
    enum class ELuaAnnotationContext : uint8
    {
        None,           // not inside an annotation comment (or typing a value)
        Directive,      // typing the directive name: `--@<here>`
        ExportArgs,     // inside `--@export( <here> )`
        RpcArgs,        // inside `--@rpc( <here> )`
        ReplicatedArgs, // inside `--@replicated( <here> )`
    };

    namespace LuaAnnotations
    {
        // The directive names (`export`, `rpc`, `replicated`).
        FLuaAnnotationTokenList Directives();

        // `--@export(...)` metadata keys (ClampMin, Category, ...).
        FLuaAnnotationTokenList ExportArgs();

        // `--@rpc(...)` specifiers (server/client/multicast, reliable/unreliable).
        FLuaAnnotationTokenList RpcArgs();

        // `--@replicated(...)` net-condition specifiers (ownerOnly/skipOwner/initialOnly).
        FLuaAnnotationTokenList ReplicatedArgs();

        // Classify the full source line `LineText` with the cursor at byte index `CursorByte`. Writes the
        // partial token currently under the cursor (for prefix matching) into OutPartial.
        ELuaAnnotationContext Classify(FStringView LineText, int CursorByte, FString& OutPartial);

        // Documentation for a directive or argument token by name (searches all three tables).
        // Returns nullptr when the name isn't part of the DSL.
        const FLuaAnnotationToken* FindToken(FStringView Name);
    }
}
