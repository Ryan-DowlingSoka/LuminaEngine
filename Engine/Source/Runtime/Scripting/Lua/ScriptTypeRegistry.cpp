#include "ScriptTypeRegistry.h"

#include <EASTL/utility.h>

namespace Lumina::Lua
{
    FScriptTypeRegistry& FScriptTypeRegistry::Get()
    {
        static FScriptTypeRegistry Instance;
        return Instance;
    }

    void FScriptTypeRegistry::RegisterClassType(FStringView Name, const TVector<FScriptTypeMember>& Members)
    {
        const FString NameStr(Name.data(), Name.size());
        for (FClassType& Existing : ClassTypes)
        {
            if (Existing.Name == NameStr)
            {
                Existing.Members = Members;
                ++Revision;
                return;
            }
        }
        ClassTypes.push_back(FClassType{ NameStr, Members });
        ++Revision;
    }

    void FScriptTypeRegistry::RegisterSnippet(FStringView TypedGlobalName, FStringView Snippet)
    {
        Snippets.emplace_back(Snippet.data(), Snippet.size());
        if (!TypedGlobalName.empty())
        {
            TypedGlobals.emplace_back(TypedGlobalName.data(), TypedGlobalName.size());
        }
        ++Revision;
    }

    bool FScriptTypeRegistry::IsTypedGlobal(FStringView Name) const
    {
        const FString NameStr(Name.data(), Name.size());
        for (const FString& Global : TypedGlobals)
        {
            if (Global == NameStr)
            {
                return true;
            }
        }
        return false;
    }

    void FScriptTypeRegistry::GetDefinitionChunks(TVector<FString>& Out) const
    {
        Out.clear();
        Out.reserve(ClassTypes.size() + Snippets.size());

        // Class types first so the global snippets below can reference them by name.
        for (const FClassType& Type : ClassTypes)
        {
            FString Chunk("export type ");
            Chunk += Type.Name;
            Chunk += " = {\n";
            for (const FScriptTypeMember& Member : Type.Members)
            {
                if (!Member.Comment.empty())
                {
                    Chunk += "    --- ";
                    Chunk += Member.Comment;
                    Chunk += "\n";
                }
                Chunk += "    ";
                Chunk += Member.Decl;
                Chunk += ",\n";
            }
            // Open indexer: methods bound without derivable types (raw functions) still resolve.
            Chunk += "    [string]: any,\n}\n";
            Out.push_back(eastl::move(Chunk));
        }

        for (const FString& Snippet : Snippets)
        {
            Out.push_back(Snippet);
        }
    }

    void FScriptTypeRegistry::GetDocEntries(TVector<FScriptDocEntry>& Out) const
    {
        Out.clear();
        for (const FClassType& Type : ClassTypes)
        {
            for (const FScriptTypeMember& Member : Type.Members)
            {
                if (Member.Comment.empty() || Member.Name.empty())
                {
                    continue;
                }
                // Must match Luau's generateDocumentationSymbols for an export type's property:
                //   "<pkg>/globaltype/<TypeName>.<member>"
                FString Symbol(kEngineScriptPackage);
                Symbol += "/globaltype/";
                Symbol += Type.Name;
                Symbol += ".";
                Symbol += Member.Name;
                Out.push_back(FScriptDocEntry{ eastl::move(Symbol), Member.Comment });
            }
        }
    }
}
