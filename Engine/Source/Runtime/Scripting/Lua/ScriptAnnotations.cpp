#include "pch.h"
#include "ScriptAnnotations.h"
#include "Log/Log.h"

namespace Lumina::Lua
{
    namespace
    {
        bool IsIdentChar(char C)
        {
            return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
        }

        FStringView Trim(FStringView S)
        {
            size_t B = 0;
            size_t E = S.size();
            while (B < E && (S[B] == ' ' || S[B] == '\t' || S[B] == '\r')) ++B;
            while (E > B && (S[E - 1] == ' ' || S[E - 1] == '\t' || S[E - 1] == '\r')) --E;
            return S.substr(B, E - B);
        }

        // Split "key=value, flag, k2=\"v 2\"" on top-level commas (respecting double quotes).
        void ParseArgs(FStringView Args, TVector<FScriptAnnotationArg>& Out)
        {
            size_t Start = 0;
            bool   bInQuotes = false;
            auto Emit = [&](FStringView Token)
            {
                FStringView T = Trim(Token);
                if (T.empty()) return;

                FScriptAnnotationArg Arg;
                const size_t Eq = T.find('=');
                if (Eq == FStringView::npos)
                {
                    Arg.Key = FName(FString(T).c_str()); // bare flag (e.g. rpc "multicast")
                }
                else
                {
                    FStringView Key = Trim(T.substr(0, Eq));
                    FStringView Val = Trim(T.substr(Eq + 1));
                    if (Val.size() >= 2 && Val.front() == '"' && Val.back() == '"')
                    {
                        Val = Val.substr(1, Val.size() - 2);
                    }
                    Arg.Key   = FName(FString(Key).c_str());
                    Arg.Value = FString(Val);
                }
                Out.push_back(eastl::move(Arg));
            };

            for (size_t i = 0; i < Args.size(); ++i)
            {
                const char C = Args[i];
                if (C == '"') bInQuotes = !bInQuotes;
                else if (C == ',' && !bInQuotes)
                {
                    Emit(Args.substr(Start, i - Start));
                    Start = i + 1;
                }
            }
            Emit(Args.substr(Start));
        }

        // Report each parameter key that newly duplicates an earlier one in the same arg list. Only
        // args at index >= NewStart are examined (args added by the current annotation line), so a key
        // is flagged once at the point the duplicate appears. No-op when Diags is null.
        void ReportDuplicateKeys(const TVector<FScriptAnnotationArg>& Args, size_t NewStart,
                                 const char* Directive, int Line, int Col, int EndCol,
                                 TVector<FScriptAnnotationDiagnostic>* Diags)
        {
            if (Diags == nullptr) return;
            for (size_t i = NewStart; i < Args.size(); ++i)
            {
                bool bDuplicate = false;
                for (size_t j = 0; j < i; ++j)
                {
                    if (Args[j].Key == Args[i].Key) { bDuplicate = true; break; }
                }
                if (!bDuplicate) continue;

                // Already flagged this key earlier in the current line's range? Skip the repeat.
                bool bAlreadyFlagged = false;
                for (size_t j = NewStart; j < i; ++j)
                {
                    if (Args[j].Key == Args[i].Key) { bAlreadyFlagged = true; break; }
                }
                if (bAlreadyFlagged) continue;

                FScriptAnnotationDiagnostic D;
                D.Line      = Line;
                D.Column    = Col;
                D.EndColumn = EndCol;
                D.Message   = FString("Duplicate '");
                D.Message  += Args[i].Key.c_str();
                D.Message  += "' parameter in @";
                D.Message  += Directive;
                D.Message  += "; each parameter may appear only once.";
                Diags->push_back(eastl::move(D));
            }
        }

        // Apply all "@name(args)" tokens on one comment line into the pending annotation. When Diags is
        // non-null, also reports duplicate parameters (Line/Col/EndCol locate the annotation comment).
        void ParseAnnotationLine(FStringView Line, FScriptMemberAnnotation& Pending, bool& bAnyPending,
                                 int DiagLine, int DiagCol, int DiagEndCol,
                                 TVector<FScriptAnnotationDiagnostic>* Diags)
        {
            // A directive that's already set on this member is a duplicate (two @export lines, etc.);
            // they should be merged into one. Reported at the line carrying the repeat.
            auto FlagDuplicateDirective = [&](const char* Directive)
            {
                if (Diags == nullptr) return;
                FScriptAnnotationDiagnostic D;
                D.Line      = DiagLine;
                D.Column    = DiagCol;
                D.EndColumn = DiagEndCol;
                D.Message   = FString("Duplicate @");
                D.Message  += Directive;
                D.Message  += " annotation on this member; combine the parameters into a single @";
                D.Message  += Directive;
                D.Message  += "(...).";
                Diags->push_back(eastl::move(D));
            };

            size_t i = 0;
            while ((i = Line.find('@', i)) != FStringView::npos)
            {
                ++i;
                const size_t NameStart = i;
                while (i < Line.size() && IsIdentChar(Line[i])) ++i;
                FStringView Name = Line.substr(NameStart, i - NameStart);

                FStringView Args;
                if (i < Line.size() && Line[i] == '(')
                {
                    const size_t Open = i + 1;
                    const size_t Close = Line.find(')', Open);
                    if (Close != FStringView::npos)
                    {
                        Args = Line.substr(Open, Close - Open);
                        i = Close + 1;
                    }
                }

                if (Name == "export")
                {
                    if (Pending.bExport) FlagDuplicateDirective("export");
                    Pending.bExport = true;
                    const size_t Before = Pending.ExportMeta.size();
                    ParseArgs(Args, Pending.ExportMeta);
                    ReportDuplicateKeys(Pending.ExportMeta, Before, "export", DiagLine, DiagCol, DiagEndCol, Diags);
                    bAnyPending = true;
                }
                else if (Name == "replicated")
                {
                    if (Pending.bReplicated) FlagDuplicateDirective("replicated");
                    Pending.bReplicated = true;
                    const size_t Before = Pending.ReplicatedArgs.size();
                    ParseArgs(Args, Pending.ReplicatedArgs);
                    ReportDuplicateKeys(Pending.ReplicatedArgs, Before, "replicated", DiagLine, DiagCol, DiagEndCol, Diags);
                    bAnyPending = true;
                }
                else if (Name == "rpc")
                {
                    if (Pending.bRpc) FlagDuplicateDirective("rpc");
                    Pending.bRpc = true;
                    const size_t Before = Pending.RpcArgs.size();
                    ParseArgs(Args, Pending.RpcArgs);
                    ReportDuplicateKeys(Pending.RpcArgs, Before, "rpc", DiagLine, DiagCol, DiagEndCol, Diags);
                    bAnyPending = true;
                }
            }
        }

        // From a declaration line, pull the table-member name: `function X:Member(`, `function X.Member(`,
        // or `X.Member =`. Returns false for locals/non-members. Sets bIsFunction.
        bool ParseMemberDecl(FStringView Line, FString& OutMember, bool& bOutIsFunction)
        {
            FStringView S = Trim(Line);

            // function <Table>[:.]<Member>(
            constexpr FStringView Fn = "function";
            if (S.size() > Fn.size() && S.substr(0, Fn.size()) == Fn && (S[Fn.size()] == ' ' || S[Fn.size()] == '\t'))
            {
                size_t i = Fn.size();
                while (i < S.size() && (S[i] == ' ' || S[i] == '\t')) ++i;
                const size_t TableStart = i;
                while (i < S.size() && IsIdentChar(S[i])) ++i;
                const bool bHasTable = (i > TableStart) && i < S.size() && (S[i] == ':' || S[i] == '.');
                if (!bHasTable)
                {
                    return false; // local function / free function
                }
                ++i; // skip : or .
                const size_t MemberStart = i;
                while (i < S.size() && IsIdentChar(S[i])) ++i;
                if (i == MemberStart) return false;
                OutMember     = FString(S.substr(MemberStart, i - MemberStart));
                bOutIsFunction = true;
                return true;
            }

            // <Table>.<Member> =   (an assignment, not a comparison)
            {
                size_t i = 0;
                const size_t TableStart = i;
                while (i < S.size() && IsIdentChar(S[i])) ++i;
                if (i > TableStart && i < S.size() && S[i] == '.')
                {
                    ++i;
                    const size_t MemberStart = i;
                    while (i < S.size() && IsIdentChar(S[i])) ++i;
                    if (i > MemberStart)
                    {
                        size_t j = i;
                        while (j < S.size() && (S[j] == ' ' || S[j] == '\t')) ++j;
                        if (j < S.size() && S[j] == '=' && (j + 1 >= S.size() || S[j + 1] != '='))
                        {
                            OutMember      = FString(S.substr(MemberStart, i - MemberStart));
                            bOutIsFunction = false;
                            return true;
                        }
                    }
                }
            }

            return false;
        }
    }

    namespace
    {
        // Shared single-pass scan. Fills OutResult with bound annotations (when non-null) and/or
        // OutDiags with validation problems (when non-null), so the scan and the validator can never
        // disagree about where an annotation binds.
        void ScanInternal(FStringView Source,
                          TVector<FScriptMemberAnnotation>* OutResult,
                          TVector<FScriptAnnotationDiagnostic>* OutDiags)
        {
            FScriptMemberAnnotation Pending;
            bool bAnyPending  = false;
            int  PendingLine   = 0; // first --@ line of the current pending block
            int  PendingCol    = 0;
            int  PendingEndCol = 0;

            size_t LineStart = 0;
            int    LineNo    = 0; // 1-based, incremented as each line is consumed below
            const size_t Len = Source.size();
            for (size_t i = 0; i <= Len; ++i)
            {
                if (i != Len && Source[i] != '\n')
                {
                    continue;
                }

                FStringView Line    = Source.substr(LineStart, i - LineStart);
                LineStart           = i + 1;
                ++LineNo;
                const FStringView Trimmed = Trim(Line);

                // 1-based column of the first non-whitespace char and end-of-line, for diagnostics.
                size_t Lead = 0;
                while (Lead < Line.size() && (Line[Lead] == ' ' || Line[Lead] == '\t' || Line[Lead] == '\r')) ++Lead;
                size_t RawEnd = Line.size();
                while (RawEnd > Lead && (Line[RawEnd - 1] == ' ' || Line[RawEnd - 1] == '\t' || Line[RawEnd - 1] == '\r')) --RawEnd;
                const int Col    = int(Lead) + 1;
                const int EndCol = int(RawEnd) + 1;

                if (Trimmed.size() >= 3 && Trimmed[0] == '-' && Trimmed[1] == '-' && Trimmed.find('@') != FStringView::npos)
                {
                    if (!bAnyPending)
                    {
                        PendingLine   = LineNo;
                        PendingCol    = Col;
                        PendingEndCol = EndCol;
                    }
                    ParseAnnotationLine(Trimmed, Pending, bAnyPending, LineNo, Col, EndCol, OutDiags);
                    continue;
                }

                // Plain comments and blank lines don't break an annotation from its declaration.
                if (Trimmed.empty() || (Trimmed.size() >= 2 && Trimmed[0] == '-' && Trimmed[1] == '-'))
                {
                    continue;
                }

                if (bAnyPending)
                {
                    FString Member;
                    bool    bIsFunction = false;
                    if (ParseMemberDecl(Trimmed, Member, bIsFunction))
                    {
                        Pending.Member      = FName(Member.c_str());
                        Pending.bIsFunction = bIsFunction;
                        Pending.Line        = LineNo;

                        // Directive bound, but to the wrong kind of declaration: @rpc is for
                        // functions; @export and @replicated are for fields. Same rules the
                        // RPC/replicated registries enforce by silently skipping.
                        if (OutDiags)
                        {
                            auto Emit = [&](const char* Msg)
                            {
                                FScriptAnnotationDiagnostic D;
                                D.Line      = PendingLine;
                                D.Column    = PendingCol;
                                D.EndColumn = PendingEndCol;
                                D.Message   = Msg;
                                OutDiags->push_back(eastl::move(D));
                            };
                            if (Pending.bRpc && !bIsFunction)
                                Emit("@rpc applies to a function; place it above a method (e.g. function Script:Method(...)).");
                            if (Pending.bReplicated && bIsFunction)
                                Emit("@replicated applies to a field; place it above a field assignment (e.g. Script.X = ...).");
                            if (Pending.bExport && bIsFunction)
                                Emit("@export applies to a field; place it above a field assignment (e.g. Script.X = ...).");
                        }

                        if (OutResult) OutResult->push_back(eastl::move(Pending));
                    }
                    else if (OutDiags)
                    {
                        FScriptAnnotationDiagnostic D;
                        D.Line      = PendingLine;
                        D.Column    = PendingCol;
                        D.EndColumn = PendingEndCol;
                        D.Message   = "Annotation is not attached to a table member. Place it directly above a field "
                                      "(e.g. Script.X = ...) or method (e.g. function Script:Method(...)).";
                        OutDiags->push_back(eastl::move(D));
                    }
                    else
                    {
                        LOG_WARN("[Script] Ignoring annotation on a non-member declaration: '{}' (annotations apply to table members only).",
                            FString(Trimmed).c_str());
                    }

                    Pending     = FScriptMemberAnnotation{};
                    bAnyPending = false;
                }
            }

            // Annotations left dangling at end of file with no declaration beneath them.
            if (bAnyPending && OutDiags)
            {
                FScriptAnnotationDiagnostic D;
                D.Line      = PendingLine;
                D.Column    = PendingCol;
                D.EndColumn = PendingEndCol;
                D.Message   = "Annotation is not attached to a table member (no declaration follows it).";
                OutDiags->push_back(eastl::move(D));
            }
        }
    }

    TVector<FScriptMemberAnnotation> ScanScriptAnnotations(FStringView Source)
    {
        TVector<FScriptMemberAnnotation> Result;
        ScanInternal(Source, &Result, nullptr);
        return Result;
    }

    TVector<FScriptAnnotationDiagnostic> ValidateScriptAnnotations(FStringView Source)
    {
        TVector<FScriptAnnotationDiagnostic> Diags;
        ScanInternal(Source, nullptr, &Diags);
        return Diags;
    }

    TVector<FScriptRpc> BuildRpcRegistry(FStringView Source)
    {
        return BuildRpcRegistry(ScanScriptAnnotations(Source));
    }

    TVector<FScriptRpc> BuildRpcRegistry(const TVector<FScriptMemberAnnotation>& Annotations)
    {
        static const FName NServer("server");
        static const FName NClient("client");
        static const FName NMulticast("multicast");
        static const FName NReliable("reliable");
        static const FName NUnreliable("unreliable");

        TVector<FScriptRpc> Registry;
        for (const FScriptMemberAnnotation& Annotation : Annotations)
        {
            if (!Annotation.bRpc || !Annotation.bIsFunction)
            {
                continue; // RPCs are functions
            }

            FScriptRpc Rpc;
            Rpc.Name = Annotation.Member;
            for (const FScriptAnnotationArg& Arg : Annotation.RpcArgs)
            {
                if      (Arg.Key == NServer)     Rpc.Mode = ERpcMode::Server;
                else if (Arg.Key == NClient)     Rpc.Mode = ERpcMode::Client;
                else if (Arg.Key == NMulticast)  Rpc.Mode = ERpcMode::Multicast;
                else if (Arg.Key == NReliable)   Rpc.bReliable = true;
                else if (Arg.Key == NUnreliable) Rpc.bReliable = false;
            }
            Registry.push_back(Rpc);
        }
        return Registry;
    }

    TVector<FScriptReplicatedField> BuildReplicatedRegistry(FStringView Source)
    {
        return BuildReplicatedRegistry(ScanScriptAnnotations(Source));
    }

    TVector<FScriptReplicatedField> BuildReplicatedRegistry(const TVector<FScriptMemberAnnotation>& Annotations)
    {
        static const FName NOwnerOnly("OwnerOnly");
        static const FName NSkipOwner("SkipOwner");
        static const FName NInitialOnly("InitialOnly");

        TVector<FScriptReplicatedField> Registry;
        for (const FScriptMemberAnnotation& Annotation : Annotations)
        {
            if (!Annotation.bReplicated || Annotation.bIsFunction)
            {
                continue; // replicated state lives on fields, not functions
            }

            FScriptReplicatedField Field;
            Field.Name = Annotation.Member;
            for (const FScriptAnnotationArg& Arg : Annotation.ReplicatedArgs)
            {
                if      (Arg.Key == NOwnerOnly)   Field.Condition = EScriptRepCondition::OwnerOnly;
                else if (Arg.Key == NSkipOwner)   Field.Condition = EScriptRepCondition::SkipOwner;
                else if (Arg.Key == NInitialOnly) Field.Condition = EScriptRepCondition::InitialOnly;
            }
            Registry.push_back(Field);
        }
        return Registry;
    }
}
