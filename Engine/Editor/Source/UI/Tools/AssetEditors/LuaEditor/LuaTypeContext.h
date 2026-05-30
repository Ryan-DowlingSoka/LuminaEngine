#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // A typed-autocomplete suggestion mapped into engine types (no Luau headers for callers);
    // Kind mirrors the single-char badges the editor's popup understands.
    struct FLuaTypedCompletion
    {
        FString Name;
        FString Detail;        // resolved type / signature text from Luau::toString
        FString Documentation; // optional doc string from BuiltinDefinitions
        char    Kind        = 'p'; // 'p' property, 'b' binding, 'k' keyword, 't' type, 's' string, 'm' module, 'v' value
        bool    bDeprecated = false;
    };

    // A type-checker error mapped from Luau::TypeError. Stricter than lint warnings: each
    // marks code the runtime can't safely execute (bad call, wrong arity, type mismatch).
    struct FLuaTypeDiagnostic
    {
        int     Line      = 1; // 1-based start line
        int     Column    = 1; // 1-based start column
        int     EndLine   = 1; // 1-based end line (inclusive)
        int     EndColumn = 1; // 1-based end column
        FString Message;       // Human-readable error text from Luau::toString.
    };

    // Owns a one-module Luau::Frontend, per editor instance (no cross-document fighting).
    // Without .d.luau registrations it covers stdlib/locals; engine globals appear as `any`.
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

        // Typed autocomplete at (Line1, Col1) (cursor just after the partial token). True if a
        // check ran; Out may still be empty (e.g. on whitespace) so callers can fall back.
        bool Autocomplete(int Line1, int Col1, TVector<FLuaTypedCompletion>& Out);

        // Inferred type at (Line1, Col1). Returns false if no expression
        // covers the position or the type couldn't be resolved.
        bool GetTypeAt(int Line1, int Col1, FString& OutType);

        // Documentation (registered via .AddComment / engine defs) for the symbol at (Line1, Col1).
        // Returns false if the position resolves to no documented symbol.
        bool GetDocAt(int Line1, int Col1, FString& OutDoc);

        // Collect type-checker errors from the last check; empty if the buffer didn't parse
        // (compile errors flow through OnScriptCompileError). Engine globals are `any`.
        void GetTypeErrors(TVector<FLuaTypeDiagnostic>& Out);

        // Register a name as both a global value binding and a type alias (both `any`) to teach
        // the Frontend about harvested engine globals, so they don't trigger Unknown*. Idempotent.
        void RegisterEngineSymbol(FStringView Name);
        void RegisterEngineSymbols(const TVector<FString>& Names);

    private:
        // Registers Luau builtins + the engine API type definitions (World.Physics, ...) into the
        // Frontend globals. Runs once; idempotent. Must precede any check or addGlobalBinding.
        void EnsureGlobals();

        struct FImpl;
        FImpl* Impl;
    };
}
