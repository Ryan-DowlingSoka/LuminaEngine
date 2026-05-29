#include "LuauAssetScan.h"

#include "Assets/AssetRegistry/AssetData.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"

#include <cctype>


namespace Lumina
{
    namespace
    {
        // Strip Luau line comments and -- comments from a buffer in place
        // by zeroing them out, so the literal scanner doesn't pick up
        // commented-out Asset.Hard("…") examples in docs.
        FString StripComments(FStringView Src)
        {
            FString Out;
            Out.reserve(Src.size());
            size_t i = 0;
            while (i < Src.size())
            {
                // Block comment --[[ ... ]]
                if (i + 3 < Src.size() && Src[i] == '-' && Src[i + 1] == '-'
                    && Src[i + 2] == '[' && Src[i + 3] == '[')
                {
                    i += 4;
                    while (i + 1 < Src.size() && !(Src[i] == ']' && Src[i + 1] == ']')) ++i;
                    if (i + 1 < Src.size()) i += 2;
                    continue;
                }
                // Line comment --
                if (i + 1 < Src.size() && Src[i] == '-' && Src[i + 1] == '-')
                {
                    while (i < Src.size() && Src[i] != '\n') ++i;
                    continue;
                }
                Out.push_back(Src[i++]);
            }
            return Out;
        }

        // Scan forward from Pos for a quoted string literal; returns its
        // contents (without quotes) or empty on miss.
        FStringView ScanQuotedString(FStringView Src, size_t Pos)
        {
            while (Pos < Src.size() && std::isspace(static_cast<unsigned char>(Src[Pos]))) ++Pos;
            if (Pos >= Src.size()) return {};
            const char Q = Src[Pos];
            if (Q != '"' && Q != '\'') return {};
            ++Pos;
            const size_t Start = Pos;
            while (Pos < Src.size() && Src[Pos] != Q && Src[Pos] != '\n') ++Pos;
            if (Pos >= Src.size() || Src[Pos] != Q) return {};
            return Src.substr(Start, Pos - Start);
        }

        // For each `Asset.<FnName>(` occurrence, capture the first
        // string-literal argument and emit it.
        void ScanCallSites(FStringView Src, FStringView FnName, TVector<FString>& Out)
        {
            FString Token = "Asset.";
            Token.append(FnName.data(), FnName.size());
            const FStringView TokenView(Token.c_str(), Token.size());

            size_t Pos = 0;
            while (Pos < Src.size())
            {
                size_t Hit = Src.find(TokenView, Pos);
                if (Hit == FStringView::npos) return;
                Pos = Hit + TokenView.size();

                // Allow whitespace + opening paren.
                size_t P = Pos;
                while (P < Src.size() && std::isspace(static_cast<unsigned char>(Src[P]))) ++P;
                if (P >= Src.size() || Src[P] != '(') continue;
                ++P;

                FStringView Path = ScanQuotedString(Src, P);
                if (!Path.empty() && Path[0] == '/')
                {
                    Out.emplace_back(Path.data(), Path.size());
                }
            }
        }

        // Bare `require("/Game/Scripts/Foo")` style calls. We deliberately
        // ignore Luau-relative requires (`./Foo`) and dotted module names
        // (`Stdlib.Foo`) — those are VM concerns and resolve against the
        // engine's hardcoded script roots; absolute VFS paths are the only
        // shape we can statically attribute to a cookable file. Mirrors the
        // runtime resolver at FScriptingContext::ResolveModulePath which
        // appends `.luau` when the user omits it.
        void ScanRequireCalls(FStringView Src, TVector<FString>& Out)
        {
            const FStringView TokenView("require");

            size_t Pos = 0;
            while (Pos < Src.size())
            {
                size_t Hit = Src.find(TokenView, Pos);
                if (Hit == FStringView::npos) return;
                Pos = Hit + TokenView.size();

                // Reject member calls (`x.require(`) and any identifier-ish
                // char immediately before `require` so we don't match the
                // tail of a longer name.
                if (Hit > 0)
                {
                    const char Prev = Src[Hit - 1];
                    const bool bIsIdentChar = (Prev == '_')
                        || std::isalnum(static_cast<unsigned char>(Prev))
                        || Prev == '.' || Prev == ':';
                    if (bIsIdentChar) continue;
                }

                size_t P = Pos;
                while (P < Src.size() && std::isspace(static_cast<unsigned char>(Src[P]))) ++P;
                if (P >= Src.size() || Src[P] != '(') continue;
                ++P;

                FStringView Path = ScanQuotedString(Src, P);
                if (Path.empty() || Path[0] != '/') continue;

                FString Resolved(Path.data(), Path.size());
                if (Resolved.size() < 5 || Resolved.compare(Resolved.size() - 5, 5, ".luau") != 0)
                {
                    Resolved += ".luau";
                }
                Out.emplace_back(Move(Resolved));
            }
        }

        bool IsLuauFile(FStringView Vp)
        {
            return VFS::Extension(Vp) == ".luau";
        }
    }


    TVector<FString> FLuauAssetScan::ExtractCandidates(FStringView Contents)
    {
        const FString Stripped = StripComments(Contents);
        const FStringView Src(Stripped.c_str(), Stripped.size());

        TVector<FString> Out;
        Out.reserve(8);
        ScanCallSites(Src, FStringView("Hard"),        Out);
        ScanCallSites(Src, FStringView("Soft"),        Out);
        ScanCallSites(Src, FStringView("LoadAsync"),   Out);
        ScanRequireCalls(Src,                          Out);
        return Out;
    }


    FLuauAssetScan::FResult FLuauAssetScan::ScanRoots(
        const TVector<FString>& VirtualRoots,
        const FAssetRegistry& Registry,
        const TFunction<void(FStringView)>& LogFunc)
    {
        FResult Result;
        THashSet<FString> UniqueAssets;
        THashSet<FString> VisitedScripts;
        TVector<FString>  Worklist;

        // Seed: every .luau under the configured content roots. Chains
        // through require() are followed below so .luau files referenced
        // from outside the seed (e.g. /Engine/Resources/Content/Scripts)
        // still get analyzed once a /Game-rooted script requires them.
        for (const FString& Root : VirtualRoots)
        {
            VFS::RecursiveDirectoryIterator(Root, [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory()) return;
                FStringView Vp(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (!IsLuauFile(Vp)) return;
                Worklist.emplace_back(Vp.data(), Vp.size());
            });
        }

        while (!Worklist.empty())
        {
            FString ScriptPath = Move(Worklist.back());
            Worklist.pop_back();

            if (VisitedScripts.find(ScriptPath) != VisitedScripts.end()) continue;
            VisitedScripts.insert(ScriptPath);

            FStringView Vp(ScriptPath.c_str(), ScriptPath.size());

            TVector<uint8> Bytes;
            if (!VFS::ReadFile(Bytes, Vp)) continue;

            ++Result.FilesScanned;

            FStringView Contents(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
            TVector<FString> Candidates = ExtractCandidates(Contents);
            Result.RawCandidates += Candidates.size();

            for (FString& Candidate : Candidates)
            {
                FStringView CandView(Candidate.c_str(), Candidate.size());

                // require() chains: a .luau candidate that exists on disk
                // is queued for its own analysis, regardless of whether the
                // registry knows about it (scripts are loose files today).
                if (IsLuauFile(CandView) && VFS::Exists(Candidate)
                    && VisitedScripts.find(Candidate) == VisitedScripts.end())
                {
                    Worklist.emplace_back(Candidate);
                }

                // Registered-asset references: hand off to the cook graph
                // exactly as before. Loose-file targets fall through here.
                if (UniqueAssets.find(Candidate) != UniqueAssets.end()) continue;
                if (Registry.GetAssetByPath(CandView) == nullptr) continue;

                if (LogFunc)
                {
                    FString Msg("  [script-ref] ");
                    Msg += Candidate;
                    Msg += "  (from ";
                    Msg.append(Vp.data(), Vp.size());
                    Msg += ")";
                    LogFunc(FStringView(Msg.c_str(), Msg.size()));
                }
                UniqueAssets.insert(Candidate);
                Result.AssetPaths.emplace_back(Move(Candidate));
                ++Result.ResolvedRefs;
            }
        }

        return Result;
    }
}
