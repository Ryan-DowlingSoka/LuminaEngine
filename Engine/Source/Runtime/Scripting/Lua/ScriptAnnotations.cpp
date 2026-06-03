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

        // Apply all "@name(args)" tokens on one comment line into the pending annotation.
        void ParseAnnotationLine(FStringView Line, FScriptMemberAnnotation& Pending, bool& bAnyPending)
        {
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
                    Pending.bExport = true;
                    ParseArgs(Args, Pending.ExportMeta);
                    bAnyPending = true;
                }
                else if (Name == "replicated")
                {
                    Pending.bReplicated = true;
                    bAnyPending = true;
                }
                else if (Name == "rpc")
                {
                    Pending.bRpc = true;
                    ParseArgs(Args, Pending.RpcArgs);
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

    TVector<FScriptMemberAnnotation> ScanScriptAnnotations(FStringView Source)
    {
        TVector<FScriptMemberAnnotation> Result;

        FScriptMemberAnnotation Pending;
        bool bAnyPending = false;

        size_t LineStart = 0;
        const size_t Len = Source.size();
        for (size_t i = 0; i <= Len; ++i)
        {
            if (i != Len && Source[i] != '\n')
            {
                continue;
            }

            FStringView Line    = Source.substr(LineStart, i - LineStart);
            LineStart           = i + 1;
            const FStringView Trimmed = Trim(Line);

            if (Trimmed.size() >= 3 && Trimmed[0] == '-' && Trimmed[1] == '-' && Trimmed.find('@') != FStringView::npos)
            {
                ParseAnnotationLine(Trimmed, Pending, bAnyPending);
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
                    Result.push_back(eastl::move(Pending));
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

        return Result;
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
}
