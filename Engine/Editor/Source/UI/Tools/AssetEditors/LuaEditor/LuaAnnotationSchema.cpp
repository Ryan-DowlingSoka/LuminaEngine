#include "LuaAnnotationSchema.h"

#include <cstring>

namespace Lumina
{
    namespace
    {
        bool IsIdent(char C)
        {
            return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')
                || (C >= '0' && C <= '9') || C == '_';
        }

        // ---- Directive names ------------------------------------------------------------------------
        const FLuaAnnotationToken kDirectives[] = {
            { "export",     "editable property",
              "Exposes the property declared on the next line in the entity's Script panel.\n"
              "Author as `Script.<Name> = <default>` and read/write at runtime as `self.<Name>`.\n"
              "Takes PROPERTY-style metadata, e.g. --@export(ClampMin=0, Category=\"Movement\").",
              true },
            { "rpc",        "networked function",
              "Marks the function on the next line as a remote procedure call. Calling it routes over the\n"
              "network per the mode/reliability specifiers, e.g. --@rpc(multicast, reliable).\n"
              "Source order is the wire id, so RPCs must be declared in the same order on every peer.",
              true },
            { "replicated", "networked field",
              "Marks the field on the next line as replicated state. The server's value is mirrored to\n"
              "clients (read-only there); define Script:OnRep_<Field>(old) to react. Optional net condition,\n"
              "e.g. --@replicated(ownerOnly). Source order is the wire rep-index (same on every peer).",
              true },
        };

        // ---- @export(...) metadata keys -------------------------------------------------------------
        const FLuaAnnotationToken kExportArgs[] = {
            { "Category",  "string", "Groups the property under a collapsible header in the panel.",                 true },
            { "Tooltip",   "string", "Hover text shown for the property in the editor.",                            true },
            { "ClampMin",  "number", "Lower bound; the editor clamps edited values to this minimum.",               true },
            { "ClampMax",  "number", "Upper bound; the editor clamps edited values to this maximum.",               true },
            { "Slider",    "flag",   "Render as a slider (requires ClampMin and ClampMax) instead of a drag.",      false },
            { "Delta",     "number", "Drag speed per pixel for numeric fields.",                                    true },
            { "Units",     "string", "Unit suffix shown after the value (e.g. \"m/s\"). Float properties only.",    true },
            { "Color",     "flag",   "Render a 3- or 4-component vector as a color picker.",                        false },
            { "Gradient",  "flag",   "Fill the slider track with a gradient.",                                      false },
            { "Glow",      "flag",   "Add a glow accent to the slider track.",                                      false },
            { "NoDrag",    "flag",   "Disable click-drag editing; type the value directly.",                        false },
            { "ReadOnly",  "flag",   "Show the value but disallow editing.",                                        false },
            { "AssetType", "string", "Render a searchable asset picker filtered to this asset type/class.",         true },
        };

        // ---- @rpc(...) specifiers -------------------------------------------------------------------
        const FLuaAnnotationToken kRpcArgs[] = {
            { "server",     "mode",        "Client -> server. Runs on the authority only.",                  false },
            { "client",     "mode",        "Server -> the owning client. Runs on that client only.",         false },
            { "multicast",  "mode",        "Server -> all clients. Runs on every connected peer.",           false },
            { "reliable",   "reliability", "Guaranteed, ordered delivery (the default).",                    false },
            { "unreliable", "reliability", "Best-effort delivery; may drop. Cheaper for high-rate calls.",   false },
        };

        // ---- @replicated(...) net conditions --------------------------------------------------------
        const FLuaAnnotationToken kReplicatedArgs[] = {
            { "OwnerOnly",   "condition", "Replicate only to the entity's owning client.",                   false },
            { "SkipOwner",   "condition", "Replicate to every client except the owner.",                     false },
            { "InitialOnly", "condition", "Send once in the spawn baseline; never in later updates.",        false },
        };

        template <int N>
        FLuaAnnotationTokenList View(const FLuaAnnotationToken (&Arr)[N])
        {
            FLuaAnnotationTokenList L;
            L.Data  = Arr;
            L.Count = N;
            return L;
        }
    }

    FLuaAnnotationTokenList LuaAnnotations::Directives()     { return View(kDirectives);     }
    FLuaAnnotationTokenList LuaAnnotations::ExportArgs()     { return View(kExportArgs);     }
    FLuaAnnotationTokenList LuaAnnotations::RpcArgs()        { return View(kRpcArgs);        }
    FLuaAnnotationTokenList LuaAnnotations::ReplicatedArgs() { return View(kReplicatedArgs); }

    ELuaAnnotationContext LuaAnnotations::Classify(FStringView LineText, int CursorByte, FString& OutPartial)
    {
        OutPartial.clear();

        const int N = static_cast<int>(LineText.size());
        const int Cursor = (CursorByte < 0) ? 0 : (CursorByte > N ? N : CursorByte);

        // Must be inside a `--` comment that starts before the cursor.
        int Comment = -1;
        for (int i = 0; i + 1 < N && i < Cursor; ++i)
        {
            if (LineText[i] == '-' && LineText[i + 1] == '-') { Comment = i; break; }
        }
        if (Comment < 0) return ELuaAnnotationContext::None;

        // Need an `@` after the comment marker and before the cursor.
        int At = -1;
        for (int i = Comment + 2; i < Cursor; ++i)
        {
            if (LineText[i] == '@') { At = i; break; }
        }
        if (At < 0) return ELuaAnnotationContext::None;

        // Directive name span right after `@`.
        int NameStart = At + 1;
        int NameEnd   = NameStart;
        while (NameEnd < N && IsIdent(LineText[NameEnd])) ++NameEnd;

        // Find the opening paren (if any) after the name, skipping spaces.
        int Paren = NameEnd;
        while (Paren < N && (LineText[Paren] == ' ' || LineText[Paren] == '\t')) ++Paren;
        const bool bHasParen = (Paren < N && LineText[Paren] == '(');

        // Cursor still on / before the directive name, and not yet into the arg list -> completing the name.
        if (Cursor <= NameEnd && (!bHasParen || Cursor <= Paren))
        {
            OutPartial = FString(LineText.data() + NameStart, Cursor - NameStart);
            return ELuaAnnotationContext::Directive;
        }

        if (!bHasParen || Cursor <= Paren) return ELuaAnnotationContext::None;

        const FStringView Name(LineText.data() + NameStart, NameEnd - NameStart);
        ELuaAnnotationContext ArgCtx = ELuaAnnotationContext::None;
        if (Name == FStringView("export")) ArgCtx = ELuaAnnotationContext::ExportArgs;
        else if (Name == FStringView("rpc")) ArgCtx = ELuaAnnotationContext::RpcArgs;
        else if (Name == FStringView("replicated")) ArgCtx = ELuaAnnotationContext::ReplicatedArgs;
        else return ELuaAnnotationContext::None; // unknown directive takes no args

        // Inside the parens: bail if the list already closed before the cursor, and find the start of the
        // current argument fragment (after the last top-level comma). Quotes are respected so a ',' or ')'
        // inside a string value doesn't confuse the split.
        int FragStart = Paren + 1;
        bool bInQuotes = false;
        for (int i = Paren + 1; i < Cursor; ++i)
        {
            const char C = LineText[i];
            if (C == '"') bInQuotes = !bInQuotes;
            else if (!bInQuotes && C == ')') return ELuaAnnotationContext::None; // past the arg list
            else if (!bInQuotes && C == ',') FragStart = i + 1;
        }

        // Within the current fragment: once a '=' (or quote) appears we're typing a value, not a key.
        int KeyStart = FragStart;
        while (KeyStart < Cursor && (LineText[KeyStart] == ' ' || LineText[KeyStart] == '\t')) ++KeyStart;
        for (int i = KeyStart; i < Cursor; ++i)
        {
            const char C = LineText[i];
            if (C == '=' || C == '"') return ELuaAnnotationContext::None;
        }

        int KeyEnd = KeyStart;
        while (KeyEnd < Cursor && IsIdent(LineText[KeyEnd])) ++KeyEnd;
        OutPartial = FString(LineText.data() + KeyStart, KeyEnd - KeyStart);
        return ArgCtx;
    }

    const FLuaAnnotationToken* LuaAnnotations::FindToken(FStringView Name)
    {
        if (Name.empty()) return nullptr;

        auto Search = [&](FLuaAnnotationTokenList List) -> const FLuaAnnotationToken*
        {
            for (const FLuaAnnotationToken& T : List)
            {
                if (Name == FStringView(T.Name, std::strlen(T.Name))) return &T;
            }
            return nullptr;
        };

        if (const FLuaAnnotationToken* T = Search(Directives()))     return T;
        if (const FLuaAnnotationToken* T = Search(ExportArgs()))     return T;
        if (const FLuaAnnotationToken* T = Search(RpcArgs()))        return T;
        if (const FLuaAnnotationToken* T = Search(ReplicatedArgs())) return T;
        return nullptr;
    }
}
