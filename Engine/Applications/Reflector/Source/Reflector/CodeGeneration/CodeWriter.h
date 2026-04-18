#pragma once
#include <cstdarg>
#include <EASTL/string.h>
#include <EASTL/string_view.h>

namespace Lumina::Reflection
{
    /**
     * String builder used by every emitter. Tracks an indent level so callers don't have
     * to splat "\t\t\t" literals throughout the codebase.
     *
     * Three families of output:
     *   - Append*(...)  : raw output, no indent, no newline
     *   - Line*(...)    : current indent + text + "\n"
     *   - Macro*(...)   : current indent + text + " \\\n" (for multi-line #define bodies)
     *
     * Blocks:
     *   BeginBlock()     -> writes "{" and pushes indent
     *   EndBlock()       -> pops indent and writes "}"
     *   EndBlockSemi()   -> pops indent and writes "};"
     */
    class FCodeWriter
    {
    public:

        FCodeWriter();
        explicit FCodeWriter(size_t InitialCapacity);

        // Raw output (no indent, no newline)
        FCodeWriter& Append(eastl::string_view Text);
        FCodeWriter& Append(const char* Text);
        FCodeWriter& Append(const eastl::string& Text);
        FCodeWriter& Appendf(const char* Fmt, ...);

        // Line output (indent + text + "\n")
        FCodeWriter& Line();
        FCodeWriter& Line(eastl::string_view Text);
        FCodeWriter& Line(const char* Text);
        FCodeWriter& Line(const eastl::string& Text);
        FCodeWriter& Linef(const char* Fmt, ...);

        // Emit several blank lines.
        FCodeWriter& BlankLines(int Count);

        // Macro continuation line (indent + text + " \\\n")
        FCodeWriter& Macro(eastl::string_view Text);
        FCodeWriter& Macro(const char* Text);
        FCodeWriter& Macrof(const char* Fmt, ...);

        // Strip the trailing " \\\n" - useful when the last line of a #define shouldn't
        // have a continuation backslash.
        void FinalizeMacro();

        // Indent control
        void PushIndent() { ++IndentLevel; }
        void PopIndent() { if (IndentLevel > 0) { --IndentLevel; } }
        int GetIndent() const { return IndentLevel; }

        // Brace blocks (auto-indented)
        FCodeWriter& BeginBlock();
        FCodeWriter& EndBlock();
        FCodeWriter& EndBlockSemi();

        // Buffer access
        const eastl::string& String() const { return Buffer; }
        eastl::string& MutableString() { return Buffer; }
        eastl::string Release() { eastl::string Out = eastl::move(Buffer); Clear(); return Out; }

        bool IsEmpty() const { return Buffer.empty(); }
        size_t Size() const { return Buffer.size(); }

        void Clear();
        void Reserve(size_t Bytes) { Buffer.reserve(Bytes); }

    private:

        void WriteIndent();
        void AppendVaList(const char* Fmt, va_list Args);

        eastl::string Buffer;
        int IndentLevel = 0;
    };

    /**
     * RAII helper that pushes an indent on construction and pops it on destruction.
     * Optionally writes "{" on entry and "}" (or "};") on exit.
     */
    class FScopedIndent
    {
    public:

        explicit FScopedIndent(FCodeWriter& InWriter)
            : Writer(InWriter)
        {
            Writer.PushIndent();
        }

        ~FScopedIndent()
        {
            Writer.PopIndent();
        }

        FScopedIndent(const FScopedIndent&) = delete;
        FScopedIndent& operator=(const FScopedIndent&) = delete;

    private:

        FCodeWriter& Writer;
    };

    class FScopedBlock
    {
    public:

        explicit FScopedBlock(FCodeWriter& InWriter, bool bInSemicolon = false)
            : Writer(InWriter)
            , bSemicolon(bInSemicolon)
        {
            Writer.BeginBlock();
        }

        ~FScopedBlock()
        {
            if (bSemicolon)
            {
                Writer.EndBlockSemi();
            }
            else
            {
                Writer.EndBlock();
            }
        }

        FScopedBlock(const FScopedBlock&) = delete;
        FScopedBlock& operator=(const FScopedBlock&) = delete;

    private:

        FCodeWriter& Writer;
        bool bSemicolon;
    };
}
