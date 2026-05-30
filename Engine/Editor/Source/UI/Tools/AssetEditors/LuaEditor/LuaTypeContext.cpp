#include "LuaTypeContext.h"

#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Autocomplete.h"
#include "Luau/AutocompleteTypes.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Documentation.h"
#include "Luau/Error.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/GlobalTypes.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"
#include "Luau/Scope.h"
#include "Luau/ToString.h"
#include "Luau/Type.h"
#include "Log/Log.h"
#include "Scripting/Lua/ScriptTypeRegistry.h"

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

        // Symbol -> comment, built from FScriptTypeRegistry. Empty-string key is the sentinel.
        Luau::DocumentationDatabase         DocDb{ std::string() };
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

    void FLuaTypeContext::EnsureGlobals()
    {
        if (Impl->bRegisteredBuiltins)
        {
            return;
        }

        // Wire stdlib into global scope for autocomplete/hover. Done once per Frontend.
        Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globals);
        Luau::registerBuiltinGlobals(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, /*forAutocomplete*/ true);

        // Engine API types come from FScriptTypeRegistry -- auto-derived class types plus the
        // World/EntityScript snippets, contributed by the bindings themselves (no hardcoded text
        // here). Each chunk loads separately so one malformed chunk can't void the rest; a failure
        // leaves that symbol as `any` (still usable), logged so it's diagnosable.
        const std::string Package(Lua::kEngineScriptPackage);
        TVector<FString> Chunks;
        Lua::FScriptTypeRegistry::Get().GetDefinitionChunks(Chunks);
        for (const FString& Chunk : Chunks)
        {
            const std::string_view Source(Chunk.c_str(), Chunk.size());
            const Luau::LoadDefinitionFileResult R1 = Impl->Frontend->loadDefinitionFile(
                Impl->Frontend->globals, Impl->Frontend->globals.globalScope,
                Source, Package, /*captureComments*/ false, /*typeCheckForAutocomplete*/ false);
            const Luau::LoadDefinitionFileResult R2 = Impl->Frontend->loadDefinitionFile(
                Impl->Frontend->globalsForAutocomplete, Impl->Frontend->globalsForAutocomplete.globalScope,
                Source, Package, /*captureComments*/ false, /*typeCheckForAutocomplete*/ true);
            if (!R1.success || !R2.success)
            {
                LOG_WARN("[LuaTypeContext] An engine type definition chunk failed to load; that symbol falls back to 'any'.");
            }
        }

        // Doc database: maps the symbols Luau derived for our typed members to the .AddComment text,
        // so hover/autocomplete can show it. Keys are built to match Luau's scheme in the registry.
        TVector<Lua::FScriptDocEntry> DocEntries;
        Lua::FScriptTypeRegistry::Get().GetDocEntries(DocEntries);
        for (const Lua::FScriptDocEntry& Entry : DocEntries)
        {
            Luau::BasicDocumentation Doc;
            Doc.documentation.assign(Entry.Text.c_str(), Entry.Text.size());
            Impl->DocDb[std::string(Entry.Symbol.c_str(), Entry.Symbol.size())] = Doc;
        }

        Impl->bRegisteredBuiltins = true;
    }

    bool FLuaTypeContext::EnsureChecked()
    {
        EnsureGlobals();

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
                // Resolve the symbol to its registered comment text (e.g. from .AddComment); leave
                // the doc blank rather than surfacing the raw symbol when there's no entry.
                if (const Luau::Documentation* Doc = Impl->DocDb.find(*E.documentationSymbol))
                {
                    if (const Luau::BasicDocumentation* Basic = Doc->get_if<Luau::BasicDocumentation>())
                    {
                        C.Documentation.assign(Basic->documentation.c_str(), Basic->documentation.size());
                    }
                }
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

    bool FLuaTypeContext::GetDocAt(int Line1, int Col1, FString& OutDoc)
    {
        OutDoc.clear();
        if (!EnsureChecked()) return false;

        const Luau::SourceModule* Source = Impl->Frontend->getSourceModule(Impl->Name);
        if (!Source) return false;

        Luau::ModulePtr Mod = Impl->Frontend->moduleResolverForAutocomplete.getModule(Impl->Name);
        if (!Mod) Mod = Impl->Frontend->moduleResolver.getModule(Impl->Name);
        if (!Mod) return false;

        const std::optional<Luau::DocumentationSymbol> Symbol =
            Luau::getDocumentationSymbolAtPosition(*Source, *Mod, FromOne(Line1, Col1));
        if (!Symbol) return false;

        if (const Luau::Documentation* Doc = Impl->DocDb.find(*Symbol))
        {
            if (const Luau::BasicDocumentation* Basic = Doc->get_if<Luau::BasicDocumentation>())
            {
                OutDoc.assign(Basic->documentation.c_str(), Basic->documentation.size());
                return !OutDoc.empty();
            }
        }
        return false;
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
        // EnsureGlobals must run first: it builds globalScope and freezes the TypeArena layout addGlobalBinding writes into.
        EnsureGlobals();
        const std::string SName(Name.data(), Name.size());
        if (Lua::FScriptTypeRegistry::Get().IsTypedGlobal(FStringView(SName.c_str(), SName.size()))) return; // precisely typed; don't clobber with any.
        RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globals,                SName);
        RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, SName);

        Impl->bDirty = true;
        Impl->Frontend->markDirty(Impl->Name);
    }

    void FLuaTypeContext::RegisterEngineSymbols(const TVector<FString>& Names)
    {
        if (Names.empty()) return;
        EnsureGlobals();
        for (const FString& Name : Names)
        {
            if (Name.empty()) continue;
            const std::string SName(Name.c_str(), Name.size());
            if (Lua::FScriptTypeRegistry::Get().IsTypedGlobal(FStringView(SName.c_str(), SName.size()))) continue; // typed; skip the any-binding.
            RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globals,                SName);
            RegisterAnyBindingInto(*Impl->Frontend, Impl->Frontend->globalsForAutocomplete, SName);
        }
        Impl->bDirty = true;
        Impl->Frontend->markDirty(Impl->Name);
    }
}
