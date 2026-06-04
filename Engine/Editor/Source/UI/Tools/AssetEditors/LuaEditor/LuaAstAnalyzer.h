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

    // A member assigned onto a table in the buffer, e.g. `Script.Thing = 0` or
    // `function Script:OnUpdate(...)`. Lets hover/autocomplete describe a field
    // ("Thing: number, field of Script") that the runtime hasn't harvested and
    // that Luau collapses to `any` on open table types.
    struct FLuaAstMemberEntry
    {
        FString Owner;          // base expression text, e.g. "Script" / "self" (empty for a plain function)
        FString Name;           // member name, e.g. "Thing"
        FString Path;           // "Owner.Name" (separator normalized to '.'), or just "Name" when Owner is empty
        FString TypeAnnotation; // explicit `: T` when one was written (locals-style)
        FString ValueHint;      // syntactic value type: number/string/boolean/.../function
        int     Line    = 1;    // 1-based line of the declaration
        bool    bMethod = false;// declared via `function Owner:Name(...)`

        // Function parameter names, when this entry is a function/method. Powers
        // signature help. Empty for non-function members.
        TVector<FString> Params;
        bool    bVararg   = false;
        bool    bFunction = false; // true for any function form (declaration or `= function`)
    };

    struct FLuaSymbolRef
    {
        int Line   = 1; // 1-based
        int Column = 1; // 1-based
        int Length = 0; // identifier length in characters
    };

    // A Luau lint warning mapped into engine types (no Luau::Lint headers for callers).
    // Produced by RunLint against the last Parse()'s AST, shared with outline/local/hover.
    struct FLuaLintWarning
    {
        // 1-based source line. Matches Luau::Location::begin.line + 1.
        int     Line   = 0;
        // 1-based column. Matches Luau::Location::begin.column + 1.
        int     Column = 0;
        // Luau lint code (Luau::LintWarning::Code value). 0 means unknown.
        int     Code   = 0;
        // Short lint family name, e.g. "TableOperations", "ForRange".
        FString Name;
        // Full message text Luau emitted.
        FString Message;
    };

    // PIMPL'd front-end for Luau's AST, keeping Luau headers out of public includes. One per
    // editor, reused across parses; the AST is valid until the next Parse() or destruction.
    class FLuaAstAnalyzer
    {
    public:
        FLuaAstAnalyzer();
        ~FLuaAstAnalyzer();

        FLuaAstAnalyzer(const FLuaAstAnalyzer&) = delete;
        FLuaAstAnalyzer& operator=(const FLuaAstAnalyzer&) = delete;

        // (Re)parse source; true if a usable tree resulted. On failure we keep the
        // partial AST Luau recovers, so most queries still work on the recovered shape.
        bool Parse(FStringView Source);

        // True if Parse() has been called and the AST root is non-null.
        bool IsValid() const;

        // Outline: DFS over function/method/local declarations and exported fields.
        // Cheap; fine to call per outline-panel render or after each debounced edit.
        void CollectOutline(TVector<FLuaAstOutlineEntry>& Out) const;

        // All buffer locals + their annotated type / inferred origin name.
        // Used by hover/autocomplete so they reflect lexical-scope locals.
        void CollectLocals(TVector<FLuaAstLocalEntry>& Out) const;

        // Members assigned onto a table in the buffer (`Script.X = ...`, method
        // declarations, `self.X = ...` inside methods). Used by hover/autocomplete
        // to describe user-authored fields the runtime hasn't harvested.
        void CollectMembers(TVector<FLuaAstMemberEntry>& Out) const;

        // Resolve the local identifier at 1-based (Line1, Col1) to its declaration (writes
        // name + line/col). Globals and member access need the type checker, not handled here.
        bool FindLocalDefinition(int Line1, int Col1, FString* OutName,
                                  int* OutDeclLine1, int* OutDeclCol1) const;

        // All references to the local at (Line1, Col1), including the
        // declaration itself. Empty if the cursor isn't on a local.
        void FindLocalReferences(int Line1, int Col1, TVector<FLuaSymbolRef>& Out) const;

        // Smart-selection: smallest enclosing AST range strictly larger than the (Cur*)
        // selection around the cursor; writes 1-based bounds. Walks outward if already matching.
        bool FindEnclosingRange(int CursorLine1, int CursorCol1,
                                int CurStartLine1, int CurStartCol1,
                                int CurEndLine1, int CurEndCol1,
                                int& OutStartLine1, int& OutStartCol1,
                                int& OutEndLine1, int& OutEndCol1) const;

        // Pretty-print source via Luau::prettyPrint into OutText; true on success.
        // On parse failure OutError carries Luau's diagnostic and OutText is empty.
        static bool Format(FStringView Source, FString& OutText, FString& OutError);

        // Lint the last Parse()'s AST; true if a pass ran (Out may be empty), false on parse
        // failure. Type-checker-dependent codes (UnknownGlobal/Type, Deprecated*) stay disabled.
        bool RunLint(TVector<FLuaLintWarning>& Out) const;

    private:
        struct FImpl;
        FImpl* Impl;
    };
}
