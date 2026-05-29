#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // One autocomplete suggestion produced by Luau's typed Autocomplete pass,
    // mapped into engine types so callers don't need Luau headers. Mirrors
    // the kinds the editor's existing popup understands (single-char badge).
    struct FLuaTypedCompletion
    {
        FString Name;
        FString Detail;        // resolved type / signature text from Luau::toString
        FString Documentation; // optional doc string from BuiltinDefinitions
        char    Kind        = 'p'; // 'p' property, 'b' binding, 'k' keyword, 't' type, 's' string, 'm' module, 'v' value
        bool    bDeprecated = false;
    };

    // One type-checker error mapped from Luau::TypeError into engine types.
    // Type errors are stricter than lint warnings: each one indicates code
    // the runtime can't safely execute (calling a non-function, wrong arity,
    // type mismatch, missing field, etc.).
    struct FLuaTypeDiagnostic
    {
        int     Line      = 1; // 1-based start line
        int     Column    = 1; // 1-based start column
        int     EndLine   = 1; // 1-based end line (inclusive)
        int     EndColumn = 1; // 1-based end column
        FString Message;       // Human-readable error text from Luau::toString.
    };

    // Owns a one-module Luau::Frontend. Each editor instance has its own
    // context so type checks don't fight across documents. Cheap to construct;
    // expensive to first-check (builtin globals get registered then).
    //
    // Without engine-specific .d.luau files registered the type checker still
    // covers stdlib + locals + inferred shapes. Engine globals (Engine, World,
    // Events, ...) appear as `any` until the user wires reflection-driven type
    // definitions.
    class FLuaTypeContext
    {
    public:
        explicit FLuaTypeContext(FStringView ModuleName);
        ~FLuaTypeContext();

        FLuaTypeContext(const FLuaTypeContext&) = delete;
        FLuaTypeContext& operator=(const FLuaTypeContext&) = delete;

        // Replace the source for this module + mark dirty. Type-check happens
        // lazily on the next query.
        void SetSource(FStringView Source);

        // Drive a check pass if the module is dirty. Returns true if a
        // module pointer was produced (parseable + at least partial check).
        bool EnsureChecked();

        // Run Luau's typed autocomplete at (Line1, Col1). The cursor uses
        // Luau's convention: position points just after the partial token.
        // Returns true iff a check produced any entries (entries may still
        // be empty - e.g. cursor on whitespace - we report that via the
        // bool so callers can fall back to the buffer-harvest popup).
        bool Autocomplete(int Line1, int Col1, TVector<FLuaTypedCompletion>& Out);

        // Inferred type at (Line1, Col1). Returns false if no expression
        // covers the position or the type couldn't be resolved.
        bool GetTypeAt(int Line1, int Col1, FString& OutType);

        // Collect type-checker errors from the most recent check. Each entry
        // points at a span of source the type checker rejected. Fills nothing
        // if the buffer didn't parse, since type-check is gated on a clean
        // parse and compile errors flow through OnScriptCompileError.
        //
        // Without engine .d.luau registrations, accesses on engine globals
        // resolve to `any` and don't trigger type errors - the checker only
        // flags genuine mismatches (calling a non-function, wrong arity on
        // a typed function, assigning a string to a typed `number`, etc.).
        void GetTypeErrors(TVector<FLuaTypeDiagnostic>& Out);

        // Register a name as a global value binding AND a type alias, both
        // resolving to `any`. Used to teach the typed Frontend about every
        // engine global the editor harvested from the live Lua VM, so:
        //   - `local x: SImpulseEvent = ...` doesn't trigger UnknownType
        //   - Reading `Engine`, `World`, etc. doesn't trigger UnknownGlobal
        //   - Calls / property access on them resolve to `any` and pass
        //     type-checking silently.
        //
        // Calling with the same name multiple times is a no-op replace.
        // Marks the module dirty so the next check picks up the new
        // bindings.
        void RegisterEngineSymbol(FStringView Name);
        void RegisterEngineSymbols(const TVector<FString>& Names);

    private:
        struct FImpl;
        FImpl* Impl;
    };
}
