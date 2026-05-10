#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // One entry in the document outline panel. Mirrors what the regex outline
    // produced before so the existing UI keeps working unchanged.
    struct FLuaAstOutlineEntry
    {
        enum class EKind : uint8 { Function, LocalFunction, Method, Local, Field };
        FString  Name;
        FString  Detail;     // signature for functions, type/value for locals
        EKind    Kind   = EKind::Local;
        int      Line   = 1; // 1-based
        int      Indent = 0; // visual indent (parent function depth)
    };

    // Local-variable declaration harvested from the AST. Replaces the regex-based
    // local index that couldn't follow scope rules.
    struct FLuaAstLocalEntry
    {
        FString Name;
        FString TypeAnnotation;  // empty when no `: T` was written
        FString OriginName;      // for `local b = a` chains - the source local
        int     Line = 1;        // 1-based line of the declaration
    };

    struct FLuaSymbolRef
    {
        int Line   = 1; // 1-based
        int Column = 1; // 1-based
        int Length = 0; // identifier length in characters
    };

    // PIMPL'd front-end for Luau's AST. Keeps Luau headers out of public
    // include surfaces (a lot of the editor includes Containers/, and we
    // don't want to drag the entire Luau type system into every TU).
    //
    // One instance per editor; reused across parses. Parse() allocates a fresh
    // backing arena and ParseResult under the hood; the AST is valid until the
    // next Parse() or destruction.
    class FLuaAstAnalyzer
    {
    public:
        FLuaAstAnalyzer();
        ~FLuaAstAnalyzer();

        FLuaAstAnalyzer(const FLuaAstAnalyzer&) = delete;
        FLuaAstAnalyzer& operator=(const FLuaAstAnalyzer&) = delete;

        // (Re)parse source. Returns true if the parse produced a usable tree.
        // Even on parse failure we keep partial AST when Luau recovers, so
        // most queries continue to work; they just see the recovered shape.
        bool Parse(FStringView Source);

        // True if Parse() has been called and the AST root is non-null.
        bool IsValid() const;

        // Outline collection - depth-first walk over function/method/local
        // declarations and exported fields. Cheap; fine to call on every
        // outline-panel render or after each (debounced) edit.
        void CollectOutline(TVector<FLuaAstOutlineEntry>& Out) const;

        // All buffer locals + their annotated type / inferred origin name.
        // Used by hover/autocomplete so they reflect lexical-scope locals.
        void CollectLocals(TVector<FLuaAstLocalEntry>& Out) const;

        // Resolve the identifier at (Line1, Col1) (1-based) to the location
        // of its declaration. Returns true on success and writes the local's
        // name + declaration line/col. Globals and member-access expressions
        // are intentionally not resolved here - those need the type checker.
        bool FindLocalDefinition(int Line1, int Col1, FString* OutName,
                                  int* OutDeclLine1, int* OutDeclCol1) const;

        // All references to the local at (Line1, Col1), including the
        // declaration itself. Empty if the cursor isn't on a local.
        void FindLocalReferences(int Line1, int Col1, TVector<FLuaSymbolRef>& Out) const;

        // Smart-selection step: find the smallest enclosing AST range strictly
        // larger than the given (Cur*) selection, around (Line1, Col1). Returns
        // true and writes 1-based (start, end) bounds on success. If the input
        // selection already matches the cursor's enclosing range, walks one
        // level outward.
        bool FindEnclosingRange(int CursorLine1, int CursorCol1,
                                int CurStartLine1, int CurStartCol1,
                                int CurEndLine1, int CurEndCol1,
                                int& OutStartLine1, int& OutStartCol1,
                                int& OutEndLine1, int& OutEndCol1) const;

        // Pretty-print the entire source via Luau::prettyPrint. Returns true
        // on success; writes the formatted text to OutText. On parse failure,
        // OutError carries Luau's diagnostic and OutText is left empty.
        static bool Format(FStringView Source, FString& OutText, FString& OutError);

    private:
        struct FImpl;
        FImpl* Impl;
    };
}
