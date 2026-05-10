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

    // Inferred type rendered as inlay text after a `local foo = ...` binding.
    // Ghost-text rendering is the editor's job; we just provide where + what.
    struct FLuaInlayHint
    {
        int     Line   = 1;     // 1-based source line of the local declaration
        int     Column = 1;     // 1-based column to anchor the hint to
        FString Text;           // ": <type>" - prefixed with ": " ready for paint
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

        // Walks the typed module for `local x = expr` declarations missing a
        // type annotation; emits the inferred type as an inlay hint anchored
        // after the local name. Out is appended to (not cleared) so callers
        // can collect multiple sources.
        void GetInlayHints(TVector<FLuaInlayHint>& Out);

    private:
        struct FImpl;
        FImpl* Impl;
    };
}
