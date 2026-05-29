#include "ClangParserContext.h"

#include <iostream>

#include "Utils.h"
#include "xxhash.h"
#include "EASTL/queue.h"

namespace Lumina::Reflection
{
    void FClangParserContext::AddReflectedMacro(FReflectionMacro&& Macro)
    {
        uint64_t Hash = XXH64(Macro.HeaderID.c_str(), strlen(Macro.HeaderID.c_str()), 0);

        eastl::vector<FReflectionMacro>& Macros = ReflectionMacros[Hash];
        Macros.push_back(eastl::move(Macro));
    }

    void FClangParserContext::AddGeneratedBodyMacro(FReflectionMacro&& Macro)
    {
        uint64_t Hash = ClangUtils::HashString(Macro.HeaderID);
        
        eastl::queue<FReflectionMacro>& Macros = GeneratedBodyMacros[Hash];
        Macros.push(eastl::move(Macro));
    }

    bool FClangParserContext::TryFindMacroForCursor(const eastl::string& HeaderID, const CXCursor& Cursor, FReflectionMacro& Macro)
    {
        uint64_t Hash = ClangUtils::HashString(HeaderID);

        auto HeaderIter = ReflectionMacros.find(Hash);
        if (HeaderIter == ReflectionMacros.end())
        {
            return false;
        }

        CXSourceRange typeRange = clang_getCursorExtent(Cursor);
        CXSourceLocation startLoc = clang_getRangeStart(typeRange);

        CXFile cursorFile;
        uint32_t cursorLine, cursorColumn;
        clang_getExpansionLocation(startLoc, &cursorFile, &cursorLine, &cursorColumn, nullptr);

        // Position is monotonic with source order within the TU; used as a tiebreaker
        // when the macro and the cursor live on the same physical line.
        const int32_t cursorPosition = (int32_t)typeRange.begin_int_data;

        CXString FileName = clang_getFileName(cursorFile);

        if (FileName.data == nullptr)
        {
            return false;
        }

        eastl::string FileNameChar = clang_getCString(FileName);
        clang_disposeString(FileName);

        // Both sides must agree on the canonical form. HeaderID is already
        // normalized; route the cursor's raw clang file name through the same
        // pipeline so case-sensitive filesystems stop dropping legitimate hits.
        FileNameChar = ClangUtils::NormalizeHeaderPath(eastl::move(FileNameChar));

        if (FileNameChar != HeaderID)
        {
            return false;
        }

        eastl::vector<FReflectionMacro>& MacrosForHeader = HeaderIter->second;

        // Prefer the closest macro that lexically precedes the cursor:
        // 1) Same line, macro before cursor in source order  (`FUNCTION(Script) float Foo()`)
        // 2) Exactly one line above                          (`FUNCTION(Script)\n float Foo()`)
        // Without (1), inline-form macros silently bind to the cursor below them and the
        // first function in a block of inline macros gets no binding at all.
        auto SameLineMatch = MacrosForHeader.end();
        auto LineAboveMatch = MacrosForHeader.end();

        for (auto iter = MacrosForHeader.begin(); iter != MacrosForHeader.end(); ++iter)
        {
            if (iter->LineNumber == cursorLine && iter->Position < cursorPosition)
            {
                if (SameLineMatch == MacrosForHeader.end() || iter->Position > SameLineMatch->Position)
                {
                    SameLineMatch = iter;
                }
            }
            else if (iter->LineNumber + 1 == cursorLine)
            {
                if (LineAboveMatch == MacrosForHeader.end() || iter->Position > LineAboveMatch->Position)
                {
                    LineAboveMatch = iter;
                }
            }
        }

        auto Best = (SameLineMatch != MacrosForHeader.end()) ? SameLineMatch : LineAboveMatch;
        if (Best != MacrosForHeader.end())
        {
            Macro = *Best;
            MacrosForHeader.erase(Best);
            return true;
        }

        return false;
    }

    bool FClangParserContext::TryFindGeneratedBodyMacro(const eastl::string& HeaderID, const CXCursor& Cursor, FReflectionMacro& Macro)
    {
        // The "this type has no GENERATED_BODY()" case is owned by the struct
        // visitor now -- it inspects the REFLECT macro for a ManualStub tag
        // and skips the requirement there. This function stays a pure lookup.
        uint64_t Hash = XXH64(HeaderID.c_str(), strlen(HeaderID.c_str()), 0);
        auto headerIter = GeneratedBodyMacros.find(Hash);
        if (headerIter == GeneratedBodyMacros.end())
        {
            Macro = {};
            return false;
        }

        
        eastl::queue<FReflectionMacro>& MacrosForHeader = headerIter->second;

        if (MacrosForHeader.empty())
        {
            Macro = {};
            return false;
        }
        
        Macro = MacrosForHeader.front();
        
        MacrosForHeader.pop();

        return true;
    }
    
    void FClangParserContext::PushNamespace(const eastl::string& Namespace)
    {
        NamespaceStack.push_back(Namespace);

        CurrentNamespace.clear();
        for (const eastl::string& String : NamespaceStack)
        {
            CurrentNamespace.append(String);
        }
    }

    void FClangParserContext::PopNamespace()
    {
        NamespaceStack.pop_back();

        CurrentNamespace.clear();
        for (const eastl::string& String : NamespaceStack)
        {
            CurrentNamespace.append(String);
        }
    }
}
