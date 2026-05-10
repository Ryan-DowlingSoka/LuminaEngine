#include "LuaTypeContext.h"

#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Autocomplete.h"
#include "Luau/AutocompleteTypes.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/GlobalTypes.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Scope.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"

#include <memory>
#include <string>

namespace Lumina
{
    namespace
    {
        // Single-buffer FileResolver: hands out our editor's source for one
        // module name and nothing else. Consumers calling require() against a
        // path the editor doesn't own get nullopt - lint/typecheck just
        // treats those modules as `any`. Good enough until we wire a real
        // project-aware resolver.
        struct FBufferFileResolver : public Luau::FileResolver
        {
            std::string OwnedName;
            std::string OwnedSource;

            std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override
            {
                if (name != OwnedName) return std::nullopt;
                Luau::SourceCode SC;
                SC.source = OwnedSource;
                SC.type   = Luau::SourceCode::Module;
                return SC;
            }

            std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override
            {
                return name;
            }
        };

        char KindBadgeFor(Luau::AutocompleteEntryKind K)
        {
            switch (K)
            {
            case Luau::AutocompleteEntryKind::Keyword:           return 'k';
            case Luau::AutocompleteEntryKind::String:            return 's';
            case Luau::AutocompleteEntryKind::Type:              return 't';
            case Luau::AutocompleteEntryKind::Module:            return 'm';
            case Luau::AutocompleteEntryKind::GeneratedFunction: return 'f';
            case Luau::AutocompleteEntryKind::Property:          return 'p';
            case Luau::AutocompleteEntryKind::Binding:           return 'b';
            case Luau::AutocompleteEntryKind::RequirePath:       return 'm';
            case Luau::AutocompleteEntryKind::HotComment:        return 'c';
            }
            return 'p';
        }

        // 1-based engine coords -> 0-based Luau coords.
        inline Luau::Position FromOne(int Line1, int Col1)
        {
            return Luau::Position(unsigned(std::max(0, Line1 - 1)),
                                  unsigned(std::max(0, Col1 - 1)));
        }
    }

    struct FLuaTypeContext::FImpl
    {
        Luau::ModuleName                    Name;
        std::unique_ptr<FBufferFileResolver> FileRes;
        std::unique_ptr<Luau::NullConfigResolver> ConfigRes;
        std::unique_ptr<Luau::Frontend>     Frontend;
        bool                                bDirty = true;
        bool                                bRegisteredBuiltins = false;
    };

    FLuaTypeContext::FLuaTypeContext(FStringView ModuleName)
        : Impl(new FImpl())
    {
        Impl->Name = std::string(ModuleName.data(), ModuleName.size());
        Impl->FileRes   = std::make_unique<FBufferFileResolver>();
        Impl->FileRes->OwnedName = Impl->Name;
        Impl->ConfigRes = std::make_unique<Luau::NullConfigResolver>();

        Luau::FrontendOptions Opts;
        Opts.retainFullTypeGraphs = true;     // we walk astTypes for inlay hints
        Opts.forAutocomplete      = true;     // tighter strict mode for richer info
        Opts.runLintChecks        = false;    // lint runs through its own path
        Impl->Frontend = std::make_unique<Luau::Frontend>(
            Impl->FileRes.get(), Impl->ConfigRes.get(), Opts);
    }

    FLuaTypeContext::~FLuaTypeContext()
    {
        delete Impl;
    }

    void FLuaTypeContext::SetSource(FStringView Source)
    {
        Impl->FileRes->OwnedSource.assign(Source.data(), Source.size());
        Impl->bDirty = true;
        Impl->Frontend->markDirty(Impl->Name);
    }

    bool FLuaTypeContext::EnsureChecked()
    {
        if (!Impl->bRegisteredBuiltins)
        {
            // Wire stdlib (math, string, table, ...) into the global scope so
            // autocomplete + hover can resolve them. Done once per Frontend.
            Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globals);
            Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, /*forAutocomplete*/ true);
            Impl->bRegisteredBuiltins = true;
        }

        if (!Impl->bDirty)
        {
            return true;
        }

        try
        {
            Impl->Frontend->check(Impl->Name);
            Impl->bDirty = false;
            return true;
        }
        catch (...)
        {
            // Frontend can throw on internal errors; we treat that as "no type
            // info available right now" and let callers fall back.
            Impl->bDirty = false; // avoid retry storm
            return false;
        }
    }

    bool FLuaTypeContext::Autocomplete(int Line1, int Col1, TVector<FLuaTypedCompletion>& Out)
    {
        Out.clear();
        if (!EnsureChecked()) return false;

        Luau::AutocompleteResult Result = Luau::autocomplete(
            *Impl->Frontend, Impl->Name, FromOne(Line1, Col1),
            /*StringCompletionCallback*/ nullptr);

        Luau::ToStringOptions ToStrOpts;
        ToStrOpts.useLineBreaks = false;

        Out.reserve(Result.entryMap.size());
        for (auto& Pair : Result.entryMap)
        {
            const Luau::AutocompleteEntry& E = Pair.second;
            FLuaTypedCompletion C;
            C.Name.assign(Pair.first.c_str(), Pair.first.size());
            C.Kind        = KindBadgeFor(E.kind);
            C.bDeprecated = E.deprecated;
            if (E.type)
            {
                std::string Ty = Luau::toString(*E.type, ToStrOpts);
                C.Detail.assign(Ty.c_str(), Ty.size());
            }
            else if (E.kind == Luau::AutocompleteEntryKind::Keyword)
            {
                C.Detail = "keyword";
            }
            if (E.documentationSymbol.has_value())
            {
                C.Documentation.assign(E.documentationSymbol->c_str(), E.documentationSymbol->size());
            }
            Out.push_back(eastl::move(C));
        }
        return true;
    }

    bool FLuaTypeContext::GetTypeAt(int Line1, int Col1, FString& OutType)
    {
        OutType.clear();
        if (!EnsureChecked()) return false;

        const Luau::SourceModule* Source = Impl->Frontend->getSourceModule(Impl->Name);
        if (!Source) return false;

        Luau::ModulePtr Mod = Impl->Frontend->moduleResolverForAutocomplete.getModule(Impl->Name);
        if (!Mod) Mod = Impl->Frontend->moduleResolver.getModule(Impl->Name);
        if (!Mod) return false;

        std::optional<Luau::TypeId> Ty = Luau::findTypeAtPosition(*Mod, *Source, FromOne(Line1, Col1));
        if (!Ty.has_value() || !*Ty) return false;

        Luau::ToStringOptions Opts;
        Opts.useLineBreaks = false;
        std::string S = Luau::toString(*Ty, Opts);
        OutType.assign(S.c_str(), S.size());
        return true;
    }

    namespace
    {
        // Walks every AstStatLocal that lacks an annotation and emits inlay
        // hints for the locals whose initializer has a resolvable type. Skips
        // anonymous-loop counters and `_` placeholders so the overlay stays
        // calm on dense code.
        struct FInlayVisitor : public Luau::AstVisitor
        {
            Luau::ModulePtr             Module;
            TVector<FLuaInlayHint>*     Out = nullptr;

            std::optional<Luau::TypeId> TypeFor(const Luau::AstExpr* Expr)
            {
                if (!Module || !Expr) return std::nullopt;
                Luau::TypeId* Slot = Module->astTypes.find(Expr);
                if (!Slot || !*Slot) return std::nullopt;
                return *Slot;
            }

            void Emit(const Luau::AstLocal* Var, std::optional<Luau::TypeId> Ty)
            {
                if (!Var || !Var->name.value || Var->annotation != nullptr) return;
                if (Var->name.value[0] == '_' && Var->name.value[1] == '\0') return;
                if (!Ty.has_value() || !*Ty) return;

                Luau::ToStringOptions Opts;
                Opts.useLineBreaks = false;
                std::string S = Luau::toString(*Ty, Opts);
                if (S.empty() || S == "any") return;

                FLuaInlayHint H;
                H.Line   = int(Var->location.begin.line) + 1;
                H.Column = int(Var->location.end.column) + 1;
                H.Text.reserve(S.size() + 2);
                H.Text.assign(": ").append(S.c_str(), S.size());
                Out->push_back(eastl::move(H));
            }

            bool visit(Luau::AstStatLocal* node) override
            {
                for (size_t I = 0; I < node->vars.size; ++I)
                {
                    const Luau::AstLocal* Var = node->vars.data[I];
                    const Luau::AstExpr*  Val = (I < node->values.size) ? node->values.data[I] : nullptr;
                    Emit(Var, TypeFor(Val));
                }
                return true;
            }
            bool visit(Luau::AstStatBlock*) override { return true; }
            bool visit(Luau::AstStat*) override     { return true; }
            bool visit(Luau::AstNode*) override     { return false; }
        };
    }

    void FLuaTypeContext::GetInlayHints(TVector<FLuaInlayHint>& Out)
    {
        if (!EnsureChecked()) return;

        Luau::SourceModule* Source = Impl->Frontend->getSourceModule(Impl->Name);
        if (!Source || !Source->root) return;

        Luau::ModulePtr Mod = Impl->Frontend->moduleResolverForAutocomplete.getModule(Impl->Name);
        if (!Mod) Mod = Impl->Frontend->moduleResolver.getModule(Impl->Name);
        if (!Mod) return;

        FInlayVisitor V;
        V.Module = Mod;
        V.Out    = &Out;
        Source->root->visit(&V);
    }
}
