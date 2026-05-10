#include "LuaLintRunner.h"

#include "Luau/Allocator.h"
#include "Luau/Ast.h"
#include "Luau/Linter.h"
#include "Luau/LinterConfig.h"
#include "Luau/ParseOptions.h"
#include "Luau/ParseResult.h"
#include "Luau/Parser.h"
#include "Luau/Scope.h"
#include "Luau/TypeArena.h"

namespace Lumina
{
    bool RunLuauLint(FStringView Source, FStringView VirtualPath, TVector<FLuaLintWarning>& Out)
    {
        Out.clear();

        if (Source.empty())
        {
            return true;
        }

        // Parser arena owns AST allocations; freed when this function returns.
        Luau::Allocator    Allocator;
        Luau::AstNameTable Names(Allocator);

        Luau::ParseOptions ParseOpts;
        ParseOpts.captureComments = true; // hot-comments (`--!nolint`, etc.) feed lint config

        Luau::ParseResult ParseResult =
            Luau::Parser::parse(Source.data(), Source.size(), Names, Allocator, ParseOpts);

        if (!ParseResult.errors.empty() || ParseResult.root == nullptr)
        {
            // Parse failure is reported through the compile-error broadcast; lint
            // against a broken parse tree would just produce noise.
            return false;
        }

        // Minimal scope so the lint passes that touch Scope (LocalShadow, ForRange,
        // etc.) have somewhere to read from. We don't run the type checker, so all
        // type-info-aware checks are deliberately disabled below.
        Luau::TypeArena Arena;
        Luau::TypePackId EmptyReturn = Arena.addTypePack({});
        Luau::ScopePtr   GlobalScope = std::make_shared<Luau::Scope>(EmptyReturn);

        Luau::LintOptions Options;
        Options.warningMask = ~uint64_t(0); // enable everything by default
        // Disable lint codes that need a real type-checker pass to be useful.
        // Without a typed module, these would either silently no-op (worst case
        // wasted CPU) or generate false-positive noise.
        Options.disableWarning(Luau::LintWarning::Code_UnknownGlobal);
        Options.disableWarning(Luau::LintWarning::Code_UnknownType);
        Options.disableWarning(Luau::LintWarning::Code_DeprecatedApi);
        Options.disableWarning(Luau::LintWarning::Code_DeprecatedGlobal);
        // FormatString and TableLiteral get less reliable without types; keep on
        // but the `if (auto ty = context->getType(...))` paths in Linter.cpp
        // self-skip when our nullptr module returns no type info.

        std::vector<Luau::LintWarning> Warnings = Luau::lint(
            ParseResult.root,
            Names,
            GlobalScope,
            /*module*/ nullptr,
            ParseResult.hotcomments,
            Options);

        Out.reserve(Warnings.size());
        for (const Luau::LintWarning& W : Warnings)
        {
            FLuaLintWarning Out_;
            // Luau locations are 0-based; the editor surfaces 1-based.
            Out_.Line    = int(W.location.begin.line) + 1;
            Out_.Column  = int(W.location.begin.column) + 1;
            Out_.Code    = int(W.code);
            Out_.Name.assign(Luau::LintWarning::getName(W.code));
            Out_.Message.assign(W.text.c_str(), W.text.size());
            Out.push_back(eastl::move(Out_));
        }

        (void)VirtualPath; // currently unused - reserved for future per-path lint config
        return true;
    }
}
