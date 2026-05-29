#include "RmlUiAssetScan.h"

#include "Assets/AssetRegistry/AssetData.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Containers/Array.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"

#include <cctype>


namespace Lumina
{
    namespace
    {
        bool IsPathChar(char c)
        {
            // Conservative VFS path char set; rejects quotes, whitespace,
            // parens and the few delimiters that close attributes / url().
            return c != '\0'
                && !std::isspace(static_cast<unsigned char>(c))
                && c != '"' && c != '\''
                && c != '(' && c != ')'
                && c != '<' && c != '>'
                && c != ';';
        }

        // Scan forward from Pos for a path-like substring starting with '/'
        // and containing at least one '.'; returns empty on miss.
        FStringView ScanPath(FStringView Src, size_t Pos)
        {
            // Skip leading whitespace and a single optional quote.
            while (Pos < Src.size() && std::isspace(static_cast<unsigned char>(Src[Pos]))) ++Pos;
            if (Pos < Src.size() && (Src[Pos] == '"' || Src[Pos] == '\'')) ++Pos;

            if (Pos >= Src.size() || Src[Pos] != '/')
            {
                return {};
            }

            const size_t Start = Pos;
            while (Pos < Src.size() && IsPathChar(Src[Pos]))
            {
                ++Pos;
            }
            FStringView Candidate = Src.substr(Start, Pos - Start);

            // Must contain an extension separator; rejects bare "/Game" / "/".
            if (Candidate.find('.') == FStringView::npos)
            {
                return {};
            }
            return Candidate;
        }

        // Find every "<Key>=" attribute occurrence and capture the path
        // that follows. Quote handling lives in ScanPath.
        void ScanAttribute(FStringView Src, FStringView Key, TVector<FString>& Out)
        {
            size_t Pos = 0;
            while (Pos < Src.size())
            {
                size_t Hit = Src.find(Key, Pos);
                if (Hit == FStringView::npos) return;
                Pos = Hit + Key.size();
                if (Pos >= Src.size() || Src[Pos] != '=')
                {
                    continue;
                }
                ++Pos;
                FStringView Path = ScanPath(Src, Pos);
                if (!Path.empty())
                {
                    Out.emplace_back(Path.data(), Path.size());
                    Pos += Path.size();
                }
            }
        }

        // CSS url(...) extractor (handles single / double / no quotes).
        void ScanCssUrl(FStringView Src, TVector<FString>& Out)
        {
            const FStringView Tok("url(");
            size_t Pos = 0;
            while (Pos < Src.size())
            {
                size_t Hit = Src.find(Tok, Pos);
                if (Hit == FStringView::npos) return;
                Pos = Hit + Tok.size();
                FStringView Path = ScanPath(Src, Pos);
                if (!Path.empty())
                {
                    Out.emplace_back(Path.data(), Path.size());
                    Pos += Path.size();
                }
            }
        }

        // Lumina-specific "material:/path/..." URI scheme used by the UI
        // material brush system (see project_ui_material_brushes).
        void ScanMaterialUri(FStringView Src, TVector<FString>& Out)
        {
            const FStringView Tok("material:");
            size_t Pos = 0;
            while (Pos < Src.size())
            {
                size_t Hit = Src.find(Tok, Pos);
                if (Hit == FStringView::npos) return;
                Pos = Hit + Tok.size();
                FStringView Path = ScanPath(Src, Pos);
                if (!Path.empty())
                {
                    Out.emplace_back(Path.data(), Path.size());
                    Pos += Path.size();
                }
            }
        }

        bool IsRmlOrRcss(FStringView VirtualPath)
        {
            FStringView Ext = VFS::Extension(VirtualPath);
            return Ext == ".rml" || Ext == ".rcss";
        }
    }


    TVector<FString> FRmlUiAssetScan::ExtractCandidates(FStringView Contents)
    {
        TVector<FString> Out;
        Out.reserve(8);

        // RML/HTML attributes that commonly carry asset paths.
        ScanAttribute(Contents, FStringView("src"),        Out);
        ScanAttribute(Contents, FStringView("href"),       Out);
        ScanAttribute(Contents, FStringView("sprite"),     Out);
        ScanAttribute(Contents, FStringView("data-asset"), Out);

        // CSS / RCSS.
        ScanCssUrl(Contents, Out);
        ScanMaterialUri(Contents, Out);

        return Out;
    }


    FRmlUiAssetScan::FResult FRmlUiAssetScan::ScanRoots(
        const TVector<FString>& VirtualRoots,
        const FAssetRegistry& Registry,
        const TFunction<void(FStringView)>& LogFunc)
    {
        FResult Result;
        THashSet<FString> UniqueAssets;
        THashSet<FString> VisitedDocs;
        TVector<FString>  Worklist;

        // Seed with every .rml/.rcss under the configured content roots,
        // then follow @import / sub-template chains. Engine UI files in
        // /Engine/Resources are reached this way once a /Game-rooted doc
        // links to them, so they don't need to be in the seed.
        for (const FString& Root : VirtualRoots)
        {
            VFS::RecursiveDirectoryIterator(Root, [&](const VFS::FFileInfo& Info)
            {
                if (Info.IsDirectory()) return;
                FStringView Vp(Info.VirtualPath.c_str(), Info.VirtualPath.size());
                if (!IsRmlOrRcss(Vp)) return;
                Worklist.emplace_back(Vp.data(), Vp.size());
            });
        }

        while (!Worklist.empty())
        {
            FString DocPath = Move(Worklist.back());
            Worklist.pop_back();

            if (VisitedDocs.find(DocPath) != VisitedDocs.end()) continue;
            VisitedDocs.insert(DocPath);

            FStringView Vp(DocPath.c_str(), DocPath.size());

            TVector<uint8> Bytes;
            if (!VFS::ReadFile(Bytes, Vp)) continue;

            ++Result.FilesScanned;

            FStringView Contents(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
            TVector<FString> Candidates = ExtractCandidates(Contents);
            Result.RawCandidates += Candidates.size();

            for (FString& Candidate : Candidates)
            {
                FStringView CandView(Candidate.c_str(), Candidate.size());

                // Transitive chain: another .rml/.rcss that exists on disk
                // gets queued for its own scan. Registry membership is not
                // required — these are loose files today.
                if (IsRmlOrRcss(CandView) && VFS::Exists(Candidate)
                    && VisitedDocs.find(Candidate) == VisitedDocs.end())
                {
                    Worklist.emplace_back(Candidate);
                }

                if (UniqueAssets.find(Candidate) != UniqueAssets.end())
                {
                    continue;
                }
                // Only paths that resolve to an actual registered asset
                // count as cook roots. .ttf/.png loose files are still
                // shipped via BundleLooseContent.
                if (Registry.GetAssetByPath(CandView) == nullptr)
                {
                    continue;
                }

                if (LogFunc)
                {
                    FString Msg("  [rml-ref] ");
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
