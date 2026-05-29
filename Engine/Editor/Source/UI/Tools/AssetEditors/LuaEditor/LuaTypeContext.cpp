#include "LuaTypeContext.h"

#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Autocomplete.h"
#include "Luau/AutocompleteTypes.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Error.h"
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
        // Resolves only the editor's own module; all other require() calls return nullopt (treated as any).
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
            // Wire stdlib into global scope for autocomplete/hover. Done once per Frontend.
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
            // Frontend can throw internally; treat as "no type info" and let callers fall back.
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

    void FLuaTypeContext::GetTypeErrors(TVector<FLuaTypeDiagnostic>& Out)
    {
        Out.clear();
        if (!EnsureChecked()) return;

        Luau::ModulePtr Mod = Impl->Frontend->moduleResolverForAutocomplete.getModule(Impl->Name);
        if (!Mod) Mod = Impl->Frontend->moduleResolver.getModule(Impl->Name);
        if (!Mod) return;

        Out.reserve(Mod->errors.size());
        for (const Luau::TypeError& Err : Mod->errors)
        {
            std::string Msg = Luau::toString(Err);

            FLuaTypeDiagnostic D;
            D.Line      = int(Err.location.begin.line) + 1;
            D.Column    = int(Err.location.begin.column) + 1;
            D.EndLine   = int(Err.location.end.line) + 1;
            D.EndColumn = int(Err.location.end.column) + 1;
            D.Message.assign(Msg.c_str(), Msg.size());
            Out.push_back(eastl::move(D));
        }
    }

    namespace
    {
        // Adds name as both a value binding and a type alias (both = any); writes into exportedTypeBindings for the type-alias side.
        void RegisterAnyBindingInto(Luau::Frontend& Frontend, Luau::GlobalTypes& Globals, const std::string& Name)
        {
            Luau::TypeId AnyTy = Frontend.builtinTypes->anyType;
            Luau::addGlobalBinding(Globals, Name, AnyTy, "@engine");
            if (Globals.globalScope)
            {
                Globals.globalScope->exportedTypeBindings[Name] = Luau::TypeFun(AnyTy);
            }
        }
    }

    void FLuaTypeContext::RegisterEngineSymbol(FStringView Name)
    {
        if (Name.empty()) return;
        // registerBuiltinGlobals must run first: it builds globalScope and freezes the TypeArena layout addGlobalBinding writes into.
        if (!Impl->bRegisteredBuiltins)
        {
            Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globals);
            Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, /*forAutocomplete*/ true);
            Impl->bRegisteredBuiltins = true;
        }
        const std::string SName(Name.data(), Name.size());
        RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globals,                SName);
        RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, SName);

        Impl->bDirty = true;
        Impl->Frontend->markDirty(Impl->Name);
    }

    void FLuaTypeContext::RegisterEngineSymbols(const TVector<FString>& Names)
    {
        if (Names.empty()) return;
        if (!Impl->bRegisteredBuiltins)
        {
            Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globals);
            Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, /*forAutocomplete*/ true);
            Impl->bRegisteredBuiltins = true;
        }
        for (const FString& Name : Names)
        {
            if (Name.empty()) continue;
            const std::string SName(Name.c_str(), Name.size());
            RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globals,                SName);
            RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, SName);
        }
        Impl->bDirty = true;
        Impl->Frontend->markDirty(Impl->Name);
    }
}
