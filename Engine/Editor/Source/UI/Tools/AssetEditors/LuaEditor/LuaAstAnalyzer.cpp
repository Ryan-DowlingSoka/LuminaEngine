#include "LuaAstAnalyzer.h"

#include "Luau/Allocator.h"
#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Linter.h"
#include "Luau/LinterConfig.h"
#include "Luau/Location.h"
#include "Luau/ParseOptions.h"
#include "Luau/ParseResult.h"
#include "Luau/Parser.h"
#include "Luau/PrettyPrinter.h"
#include "Luau/Scope.h"
#include "Luau/TypeArena.h"

#include <memory>
#include <string>

namespace Lumina
{
    // ---------------------------------------------------------------------
    //  PIMPL state
    // ---------------------------------------------------------------------
    struct FLuaAstAnalyzer::FImpl
    {
        std::unique_ptr<Luau::Allocator>    Allocator;
        std::unique_ptr<Luau::AstNameTable> Names;
        Luau::ParseResult                   ParseResult;
        bool                                bValid = false;

        Luau::AstStatBlock* Root() const { return ParseResult.root; }
    };

    namespace
    {
        // Convert a Luau::Location (0-based) to 1-based source coords.
        inline int ToOneLine(const Luau::Position& P)   { return int(P.line) + 1; }
        inline int ToOneColumn(const Luau::Position& P) { return int(P.column) + 1; }

        // (Line1, Col1) is 1-based; Luau::Position is 0-based.
        inline Luau::Position FromOne(int Line1, int Col1)
        {
            return Luau::Position(unsigned(std::max(0, Line1 - 1)),
                                  unsigned(std::max(0, Col1 - 1)));
        }

        // True if Loc strictly contains Inner (Inner is fully inside Loc and they
        // are not equal).
        bool StrictlyContains(const Luau::Location& Outer, const Luau::Location& Inner)
        {
            if (!(Outer.begin <= Inner.begin) || !(Inner.end <= Outer.end))
            {
                return false;
            }
            return (Outer.begin < Inner.begin) || (Inner.end < Outer.end);
        }

        // Translate an AstName into an FString. AstName::value is a NUL-terminated
        // pointer into the AstNameTable's interned storage; safe while the
        // analyzer lives.
        inline FString FromAstName(Luau::AstName Name)
        {
            return FString(Name.value ? Name.value : "");
        }

        // Walk an AstExpr that names a function (Foo, Foo.Bar, Foo:Method) and
        // produce its dotted-form display name plus a colon flag for methods.
        void RenderFunctionName(Luau::AstExpr* Expr, std::string& Out, bool& bIsMethod)
        {
            bIsMethod = false;
            if (Expr == nullptr) return;

            if (auto* Global = Expr->as<Luau::AstExprGlobal>())
            {
                Out.append(Global->name.value ? Global->name.value : "");
                return;
            }
            if (auto* Local = Expr->as<Luau::AstExprLocal>())
            {
                Out.append(Local->local && Local->local->name.value ? Local->local->name.value : "");
                return;
            }
            if (auto* Idx = Expr->as<Luau::AstExprIndexName>())
            {
                RenderFunctionName(Idx->expr, Out, bIsMethod);
                Out.push_back(Idx->op);
                Out.append(Idx->index.value ? Idx->index.value : "");
                if (Idx->op == ':') bIsMethod = true;
                return;
            }
        }

        // Build "(arg1, arg2, ...)" from an AstExprFunction. Cheap; no types.
        std::string RenderFunctionParams(const Luau::AstExprFunction& Fn)
        {
            std::string Out = "(";
            bool bFirst = true;
            for (Luau::AstLocal* Arg : Fn.args)
            {
                if (!bFirst) Out.append(", ");
                bFirst = false;
                Out.append(Arg && Arg->name.value ? Arg->name.value : "?");
            }
            if (Fn.vararg)
            {
                if (!bFirst) Out.append(", ");
                Out.append("...");
            }
            Out.push_back(')');
            return Out;
        }

        // Stringify a type annotation for use as a key into the editor's
        // SymbolsByPath map and as hover text. The generic Luau::toString
        // routes through PrettyPrinter, which uses location-aware advance
        // and pads the result with leading whitespace / newlines from the
        // source. We need a clean identifier-shaped string, so we walk the
        // common AST type shapes by hand and only fall back to toString
        // for the rare structural-type cases.
        FString FormatTypeAnnotation(Luau::AstType* Annotation)
        {
            if (Annotation == nullptr) return {};

            if (auto* Ref = Annotation->as<Luau::AstTypeReference>())
            {
                FString Out;
                if (Ref->prefix.has_value() && Ref->prefix->value)
                {
                    Out.assign(Ref->prefix->value);
                    Out.push_back('.');
                }
                if (Ref->name.value)
                {
                    Out.append(Ref->name.value);
                }
                return Out;
            }

            // Other shapes (table types, unions, function types, ...) - fall
            // back to the generic stringifier and trim leading whitespace
            // emitted by the location-aware writer.
            std::string Anno = Luau::toString(Annotation);
            size_t First = 0;
            while (First < Anno.size()
                && (Anno[First] == ' ' || Anno[First] == '\t' ||
                    Anno[First] == '\r' || Anno[First] == '\n'))
            {
                ++First;
            }
            size_t Last = Anno.size();
            while (Last > First
                && (Anno[Last - 1] == ' ' || Anno[Last - 1] == '\t' ||
                    Anno[Last - 1] == '\r' || Anno[Last - 1] == '\n'))
            {
                --Last;
            }
            return FString(Anno.data() + First, Last - First);
        }

        // Cheap value-hint snapshot from an AstExpr's syntactic shape. Mirrors
        // what the old regex parser produced - we do this without the type
        // checker so unrelated lints don't have to wait on a Frontend pass.
        const char* InferValueHint(Luau::AstExpr* Expr)
        {
            if (Expr == nullptr) return nullptr;
            if (Expr->is<Luau::AstExprConstantString>()) return "string";
            if (Expr->is<Luau::AstExprConstantNumber>()) return "number";
            if (Expr->is<Luau::AstExprConstantBool>())   return "boolean";
            if (Expr->is<Luau::AstExprConstantNil>())    return "nil";
            if (Expr->is<Luau::AstExprTable>())          return "table";
            if (Expr->is<Luau::AstExprFunction>())       return "function";
            if (Expr->is<Luau::AstExprInterpString>())   return "string";
            return nullptr;
        }
    }

    // ---------------------------------------------------------------------
    //  Outline / locals visitors
    // ---------------------------------------------------------------------
    namespace
    {
        struct FOutlineVisitor : public Luau::AstVisitor
        {
            TVector<FLuaAstOutlineEntry>* Out;
            int FunctionDepth = 0;

            // We don't visit AstExprFunction blindly; only when entered through
            // AstStat* declarations. Lambdas otherwise pollute the outline.
            bool visit(Luau::AstStatLocalFunction* node) override
            {
                FLuaAstOutlineEntry E;
                E.Kind   = FLuaAstOutlineEntry::EKind::LocalFunction;
                E.Name   = FromAstName(node->name->name);
                E.Detail = FString(RenderFunctionParams(*node->func).c_str());
                E.Line   = ToOneLine(node->location.begin);
                E.Indent = FunctionDepth;
                Out->push_back(eastl::move(E));

                ++FunctionDepth;
                if (node->func && node->func->body) node->func->body->visit(this);
                --FunctionDepth;
                return false;
            }

            bool visit(Luau::AstStatFunction* node) override
            {
                std::string DisplayName;
                bool bIsMethod = false;
                RenderFunctionName(node->name, DisplayName, bIsMethod);

                FLuaAstOutlineEntry E;
                E.Kind   = bIsMethod ? FLuaAstOutlineEntry::EKind::Method
                                     : FLuaAstOutlineEntry::EKind::Function;
                E.Name   = FString(DisplayName.c_str(), DisplayName.size());
                E.Detail = FString(RenderFunctionParams(*node->func).c_str());
                E.Line   = ToOneLine(node->location.begin);
                E.Indent = FunctionDepth;
                Out->push_back(eastl::move(E));

                ++FunctionDepth;
                if (node->func && node->func->body) node->func->body->visit(this);
                --FunctionDepth;
                return false;
            }

            bool visit(Luau::AstStatLocal* node) override
            {
                // Surface only top-level (depth==0) named locals; deeper locals
                // would crowd the outline. Also skip when paired with a function
                // initializer - that path goes through AstStatLocalFunction.
                if (FunctionDepth != 0) return true;

                for (size_t I = 0; I < node->vars.size; ++I)
                {
                    Luau::AstLocal* Var = node->vars.data[I];
                    if (Var == nullptr) continue;
                    Luau::AstExpr* Val = (I < node->values.size) ? node->values.data[I] : nullptr;
                    if (Val && Val->is<Luau::AstExprFunction>())
                    {
                        // Inline `local F = function(...)` shows up as a function entry.
                        FLuaAstOutlineEntry E;
                        E.Kind   = FLuaAstOutlineEntry::EKind::LocalFunction;
                        E.Name   = FromAstName(Var->name);
                        E.Detail = FString(RenderFunctionParams(*Val->as<Luau::AstExprFunction>()).c_str());
                        E.Line   = ToOneLine(node->location.begin);
                        E.Indent = FunctionDepth;
                        Out->push_back(eastl::move(E));
                        continue;
                    }

                    FLuaAstOutlineEntry E;
                    E.Kind   = FLuaAstOutlineEntry::EKind::Local;
                    E.Name   = FromAstName(Var->name);
                    if (const char* Hint = InferValueHint(Val))
                    {
                        E.Detail.assign(": ").append(Hint);
                    }
                    E.Line   = ToOneLine(node->location.begin);
                    E.Indent = FunctionDepth;
                    Out->push_back(eastl::move(E));
                }
                return true;
            }

            bool visit(Luau::AstStatBlock*) override { return true; }
            bool visit(Luau::AstStat*) override     { return true; }
            bool visit(Luau::AstNode*) override     { return false; }
        };

        struct FLocalsVisitor : public Luau::AstVisitor
        {
            TVector<FLuaAstLocalEntry>* Out;

            void Push(Luau::AstLocal* Var, Luau::AstExpr* Val)
            {
                if (Var == nullptr || Var->name.value == nullptr) return;
                FLuaAstLocalEntry E;
                E.Name = FromAstName(Var->name);
                E.Line = ToOneLine(Var->location.begin);
                if (Var->annotation != nullptr)
                {
                    E.TypeAnnotation = FormatTypeAnnotation(Var->annotation);
                }
                else if (const char* Hint = InferValueHint(Val))
                {
                    E.TypeAnnotation = Hint;
                }
                else if (Val)
                {
                    if (auto* RhsLocal = Val->as<Luau::AstExprLocal>(); RhsLocal && RhsLocal->local)
                    {
                        E.OriginName = FromAstName(RhsLocal->local->name);
                    }
                }
                Out->push_back(eastl::move(E));
            }

            bool visit(Luau::AstStatLocal* node) override
            {
                for (size_t I = 0; I < node->vars.size; ++I)
                {
                    Luau::AstExpr* Val = (I < node->values.size) ? node->values.data[I] : nullptr;
                    Push(node->vars.data[I], Val);
                }
                return true;
            }
            bool visit(Luau::AstStatLocalFunction* node) override
            {
                FLuaAstLocalEntry E;
                E.Name = FromAstName(node->name->name);
                E.Line = ToOneLine(node->name->location.begin);
                E.TypeAnnotation = "function";
                Out->push_back(eastl::move(E));
                return true;
            }
            bool visit(Luau::AstStatFor* node) override
            {
                FLuaAstLocalEntry E;
                E.Name = FromAstName(node->var->name);
                E.Line = ToOneLine(node->var->location.begin);
                E.TypeAnnotation = "number";
                Out->push_back(eastl::move(E));
                return true;
            }
            bool visit(Luau::AstStatForIn* node) override
            {
                for (size_t I = 0; I < node->vars.size; ++I)
                {
                    Push(node->vars.data[I], nullptr);
                }
                return true;
            }
            bool visit(Luau::AstExprFunction* node) override
            {
                if (node->self) Push(node->self, nullptr);
                for (size_t I = 0; I < node->args.size; ++I)
                {
                    Push(node->args.data[I], nullptr);
                }
                return true;
            }
            bool visit(Luau::AstStatBlock*) override { return true; }
            bool visit(Luau::AstStat*) override     { return true; }
            bool visit(Luau::AstNode*) override     { return false; }
        };

        // Locate an AstExprLocal whose location encloses (Pos). The walker also
        // tracks AstLocals at their declaration site so a hover on the declared
        // name (which isn't an AstExprLocal) still resolves.
        struct FResolveLocalVisitor : public Luau::AstVisitor
        {
            Luau::Position    Pos = {0, 0};
            Luau::AstLocal*   Hit = nullptr;
            Luau::Location    HitLocation;

            bool Contains(const Luau::Location& Loc) const
            {
                return Loc.begin <= Pos && Pos < Loc.end;
            }

            void TryDeclSite(Luau::AstLocal* Local)
            {
                if (Local == nullptr) return;
                if (Contains(Local->location))
                {
                    Hit = Local;
                    HitLocation = Local->location;
                }
            }

            bool visit(Luau::AstStatLocal* node) override
            {
                for (Luau::AstLocal* V : node->vars) TryDeclSite(V);
                return true;
            }
            bool visit(Luau::AstStatLocalFunction* node) override
            {
                TryDeclSite(node->name);
                return true;
            }
            bool visit(Luau::AstStatFor* node) override     { TryDeclSite(node->var); return true; }
            bool visit(Luau::AstStatForIn* node) override
            {
                for (Luau::AstLocal* V : node->vars) TryDeclSite(V);
                return true;
            }
            bool visit(Luau::AstExprFunction* node) override
            {
                if (node->self) TryDeclSite(node->self);
                for (Luau::AstLocal* A : node->args) TryDeclSite(A);
                return true;
            }
            bool visit(Luau::AstExprLocal* node) override
            {
                if (Contains(node->location))
                {
                    Hit = node->local;
                    HitLocation = node->local ? node->local->location : node->location;
                }
                return true;
            }
            bool visit(Luau::AstStatBlock*) override { return true; }
            bool visit(Luau::AstStat*) override     { return true; }
            bool visit(Luau::AstExpr*) override     { return true; }
            bool visit(Luau::AstNode*) override     { return false; }
        };

        // Collect every AstExprLocal whose `local` field == Target, plus the
        // declaration site itself.
        struct FReferenceVisitor : public Luau::AstVisitor
        {
            Luau::AstLocal*       Target = nullptr;
            TVector<FLuaSymbolRef>* Out = nullptr;

            void Add(const Luau::Location& Loc, int IdentLen)
            {
                FLuaSymbolRef R;
                R.Line   = ToOneLine(Loc.begin);
                R.Column = ToOneColumn(Loc.begin);
                R.Length = IdentLen;
                Out->push_back(R);
            }

            int NameLen() const
            {
                return (Target && Target->name.value) ? int(std::strlen(Target->name.value)) : 0;
            }

            bool visit(Luau::AstStatLocal* node) override
            {
                for (Luau::AstLocal* V : node->vars)
                {
                    if (V == Target) Add(V->location, NameLen());
                }
                return true;
            }
            bool visit(Luau::AstStatLocalFunction* node) override
            {
                if (node->name == Target) Add(node->name->location, NameLen());
                return true;
            }
            bool visit(Luau::AstStatFor* node) override
            {
                if (node->var == Target) Add(node->var->location, NameLen());
                return true;
            }
            bool visit(Luau::AstStatForIn* node) override
            {
                for (Luau::AstLocal* V : node->vars)
                {
                    if (V == Target) Add(V->location, NameLen());
                }
                return true;
            }
            bool visit(Luau::AstExprFunction* node) override
            {
                if (node->self == Target) Add(node->self->location, NameLen());
                for (Luau::AstLocal* A : node->args)
                {
                    if (A == Target) Add(A->location, NameLen());
                }
                return true;
            }
            bool visit(Luau::AstExprLocal* node) override
            {
                if (node->local == Target) Add(node->location, NameLen());
                return true;
            }
            bool visit(Luau::AstStatBlock*) override { return true; }
            bool visit(Luau::AstStat*) override     { return true; }
            bool visit(Luau::AstExpr*) override     { return true; }
            bool visit(Luau::AstNode*) override     { return false; }
        };
    }

    // ---------------------------------------------------------------------
    //  Public API
    // ---------------------------------------------------------------------
    FLuaAstAnalyzer::FLuaAstAnalyzer()
        : Impl(new FImpl())
    {
    }

    FLuaAstAnalyzer::~FLuaAstAnalyzer()
    {
        delete Impl;
    }

    bool FLuaAstAnalyzer::Parse(FStringView Source)
    {
        Impl->bValid = false;
        Impl->ParseResult = {};
        Impl->Allocator = std::make_unique<Luau::Allocator>();
        Impl->Names     = std::make_unique<Luau::AstNameTable>(*Impl->Allocator);

        Luau::ParseOptions Opts;
        Opts.captureComments = true;

        Impl->ParseResult = Luau::Parser::parse(Source.data(), Source.size(),
                                                *Impl->Names, *Impl->Allocator, Opts);
        Impl->bValid = (Impl->ParseResult.root != nullptr);
        return Impl->bValid && Impl->ParseResult.errors.empty();
    }

    bool FLuaAstAnalyzer::IsValid() const
    {
        return Impl != nullptr && Impl->bValid && Impl->Root() != nullptr;
    }

    void FLuaAstAnalyzer::CollectOutline(TVector<FLuaAstOutlineEntry>& Out) const
    {
        Out.clear();
        if (!IsValid()) return;
        FOutlineVisitor V;
        V.Out = &Out;
        Impl->Root()->visit(&V);
    }

    void FLuaAstAnalyzer::CollectLocals(TVector<FLuaAstLocalEntry>& Out) const
    {
        Out.clear();
        if (!IsValid()) return;
        FLocalsVisitor V;
        V.Out = &Out;
        Impl->Root()->visit(&V);
    }

    bool FLuaAstAnalyzer::FindLocalDefinition(int Line1, int Col1, FString* OutName,
                                               int* OutDeclLine1, int* OutDeclCol1) const
    {
        if (!IsValid()) return false;
        FResolveLocalVisitor V;
        V.Pos = FromOne(Line1, Col1);
        Impl->Root()->visit(&V);
        if (V.Hit == nullptr) return false;

        if (OutName)
        {
            *OutName = FromAstName(V.Hit->name);
        }
        if (OutDeclLine1) *OutDeclLine1 = ToOneLine(V.Hit->location.begin);
        if (OutDeclCol1)  *OutDeclCol1  = ToOneColumn(V.Hit->location.begin);
        return true;
    }

    void FLuaAstAnalyzer::FindLocalReferences(int Line1, int Col1, TVector<FLuaSymbolRef>& Out) const
    {
        Out.clear();
        if (!IsValid()) return;

        FResolveLocalVisitor R;
        R.Pos = FromOne(Line1, Col1);
        Impl->Root()->visit(&R);
        if (R.Hit == nullptr) return;

        FReferenceVisitor V;
        V.Target = R.Hit;
        V.Out    = &Out;
        Impl->Root()->visit(&V);
    }

    bool FLuaAstAnalyzer::FindEnclosingRange(int CursorLine1, int CursorCol1,
                                              int CurStartLine1, int CurStartCol1,
                                              int CurEndLine1, int CurEndCol1,
                                              int& OutStartLine1, int& OutStartCol1,
                                              int& OutEndLine1, int& OutEndCol1) const
    {
        if (!IsValid()) return false;

        Luau::Location Current{
            FromOne(CurStartLine1, CurStartCol1),
            FromOne(CurEndLine1, CurEndCol1)
        };
        Luau::Position Cursor = FromOne(CursorLine1, CursorCol1);

        // Walk the ancestry from outer to inner; return the smallest one that
        // strictly contains the current selection.
        std::vector<Luau::AstNode*> Ancestry =
            Luau::findAstAncestryOfPosition(Impl->Root(), Cursor, /*includeTypes*/ true);

        for (auto It = Ancestry.rbegin(); It != Ancestry.rend(); ++It)
        {
            if (StrictlyContains((*It)->location, Current))
            {
                OutStartLine1 = ToOneLine((*It)->location.begin);
                OutStartCol1  = ToOneColumn((*It)->location.begin);
                OutEndLine1   = ToOneLine((*It)->location.end);
                OutEndCol1    = ToOneColumn((*It)->location.end);
                return true;
            }
        }
        return false;
    }

    bool FLuaAstAnalyzer::Format(FStringView Source, FString& OutText, FString& OutError)
    {
        OutText.clear();
        OutError.clear();

        Luau::ParseOptions Opts;
        Opts.captureComments = false;

        Luau::PrettyPrintResult Pretty = Luau::prettyPrint(
            std::string_view(Source.data(), Source.size()), Opts, /*withTypes*/ false);

        if (!Pretty.parseError.empty())
        {
            OutError.assign(Pretty.parseError.c_str(), Pretty.parseError.size());
            return false;
        }
        OutText.assign(Pretty.code.c_str(), Pretty.code.size());
        return true;
    }

    bool FLuaAstAnalyzer::RunLint(TVector<FLuaLintWarning>& Out) const
    {
        Out.clear();
        if (!IsValid()) return false;
        if (!Impl->ParseResult.errors.empty()) return false; // syntactic failure - skip lint

        // Minimal scope so the lint passes that touch Scope (LocalShadow,
        // ForRange, ...) have somewhere to read from. We don't run the
        // type checker, so all type-info-aware checks are deliberately
        // disabled below.
        Luau::TypeArena    Arena;
        Luau::TypePackId   EmptyReturn = Arena.addTypePack({});
        Luau::ScopePtr     GlobalScope = std::make_shared<Luau::Scope>(EmptyReturn);

        Luau::LintOptions Options;
        Options.warningMask = ~uint64_t(0);
        Options.disableWarning(Luau::LintWarning::Code_UnknownGlobal);
        Options.disableWarning(Luau::LintWarning::Code_UnknownType);
        Options.disableWarning(Luau::LintWarning::Code_DeprecatedApi);
        Options.disableWarning(Luau::LintWarning::Code_DeprecatedGlobal);

        std::vector<Luau::LintWarning> Warnings = Luau::lint(
            Impl->Root(),
            *Impl->Names,
            GlobalScope,
            /*module*/ nullptr,
            Impl->ParseResult.hotcomments,
            Options);

        Out.reserve(Warnings.size());
        for (const Luau::LintWarning& W : Warnings)
        {
            FLuaLintWarning E;
            E.Line    = ToOneLine(W.location.begin);
            E.Column  = ToOneColumn(W.location.begin);
            E.Code    = int(W.code);
            E.Name.assign(Luau::LintWarning::getName(W.code));
            E.Message.assign(W.text.c_str(), W.text.size());
            Out.push_back(eastl::move(E));
        }
        return true;
    }
}
