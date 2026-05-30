#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Platform/GenericPlatform.h"

namespace Lumina::Lua
{
    // Package name the editor passes to Luau::loadDefinitionFile for engine defs. Luau derives doc
    // symbols from it as "<pkg>/globaltype/<Type>.<member>" (export types) -- the doc database keys
    // must match, so both the editor loader and the registry's doc keys use this single constant.
    inline constexpr const char* kEngineScriptPackage = "@engine";

    // One field of an auto-derived editor type: the field name, its Luau declaration, plus optional
    // editor metadata (doc comment, ...). Built by the binding builder; never reaches a non-editor runtime.
    struct FScriptTypeMember
    {
        FString Name;     // "AddForce" -- used to form the Luau doc symbol
        FString Decl;     // "AddForce: (self: PhysicsScene, p0: number, p1: vector) -> ()"
        FString Comment;  // optional doc, surfaced on hover/completion; empty if none
    };

    // A resolved documentation entry: the Luau doc symbol and its text. Consumed by the editor to
    // build a DocumentationDatabase so hover/autocomplete can show the comment.
    struct FScriptDocEntry
    {
        FString Symbol;
        FString Text;
    };

    // Central registry of Luau type definitions for the script editor's analyzer. Engine bindings
    // contribute their type surface here instead of the editor hand-maintaining a parallel block of
    // definition text:
    //   - Class/userdata types are AUTO-DERIVED from the binding builder (TClass::AddFunction), so a
    //     method's signature comes straight from its C++ function type and can never drift.
    //   - Table globals and Lua-side base types (World, EntityScript, ...) are registered as snippets
    //     next to where they're set up.
    // The editor pulls GetDefinitionChunks() and loads each into the Luau Frontend. One registry,
    // one source of truth, no hardcoded editor strings.
    class RUNTIME_API FScriptTypeRegistry
    {
    public:

        static FScriptTypeRegistry& Get();

        // Auto-derived userdata/class type. Members carry the Luau field decl + optional metadata.
        // Re-registering the same name replaces it.
        void RegisterClassType(FStringView Name, const TVector<FScriptTypeMember>& Members);

        // A verbatim Luau definition snippet (declare / export type / ...). TypedGlobalName is the
        // top-level value symbol it precisely types (e.g. "World"), so the symbol harvester won't
        // shadow it with `any`; pass empty if the snippet declares no global value.
        void RegisterSnippet(FStringView TypedGlobalName, FStringView Snippet);

        // True for symbols a snippet precisely types -- the editor skips the `any` fallback binding.
        bool IsTypedGlobal(FStringView Name) const;

        // Independently-loadable definition chunks in dependency order (class types first so globals
        // may reference them, then snippets). Loaded separately so one malformed chunk can't void the
        // rest. Bumps nothing; cheap to call.
        void GetDefinitionChunks(TVector<FString>& Out) const;

        // Doc symbol -> comment text for every commented member, keyed to match Luau's auto-derived
        // symbols ("<pkg>/globaltype/<Type>.<member>"). The editor turns these into a doc database.
        void GetDocEntries(TVector<FScriptDocEntry>& Out) const;

        // Increments whenever a registration changes; lets the editor reload defs after late
        // registrations (e.g. runtime component types discovered post-init).
        uint64 GetRevision() const { return Revision; }

    private:

        struct FClassType
        {
            FString                     Name;
            TVector<FScriptTypeMember>  Members;
        };

        TVector<FClassType> ClassTypes;
        TVector<FString>    Snippets;
        TVector<FString>    TypedGlobals;
        uint64              Revision = 0;
    };
}
