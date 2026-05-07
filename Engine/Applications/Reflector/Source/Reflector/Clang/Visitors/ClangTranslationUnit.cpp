#include "ClangTranslationUnit.h"
#include "ClangVisitor.h"
#include "Reflector/Clang/ClangParserContext.h"
#include "Reflector/Clang/Utils.h"
#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <StringHash.h>
#include <clang-c/CXSourceLocation.h>
#include <cstdint>

namespace Lumina::Reflection
{
	uint64_t GTranslationUnitsVisited;
	uint64_t GTranslationUnitsParsed;

	CXChildVisitResult VisitTranslationUnit(CXCursor Cursor, CXCursor Parent, CXClientData ClientData)
	{
		GTranslationUnitsVisited++;

		CXSourceLocation Loc = clang_getCursorLocation(Cursor);
		if (clang_Location_isInSystemHeader(Loc))
		{
			return CXChildVisit_Continue;
		}

		FClangParserContext* ParserContext = (FClangParserContext*)ClientData;

		eastl::string FilePath = ClangUtils::GetHeaderPathForCursor(Cursor);
		if (FilePath.empty())
		{
			return CXChildVisit_Continue;
		}

		FStringHash Hash(FilePath);
		auto Itr = ParserContext->AllHeaders.find(Hash);
		if (Itr == ParserContext->AllHeaders.end())
		{
			// Manual-reflect cursors that fail to bind here indicate a path
			// normalization mismatch between the JSON registration side and
			// libclang's reported file path. When this hits, no struct is
			// registered, and consumers still emit a forward decl + take the
			// address of the (never-defined) Construct_CStruct_glm_* function,
			// producing a confusing "undeclared identifier" cascade in dependent
			// modules. Surface it loudly so the failure is diagnosable.
			if (FilePath.find("manualreflecttypes") != eastl::string::npos)
			{
				static bool bWarned = false;
				if (!bWarned)
				{
					bWarned = true;
					spdlog::error("ManualReflectTypes cursor at '{}' did not match any registered header — glm reflection will be missing. Check LUMINA_DIR canonicalization.", FilePath.c_str());
				}
			}
			return CXChildVisit_Continue;
		}

		GTranslationUnitsParsed++;

		ParserContext->ReflectedHeader = Itr->second;

		CXCursorKind CursorKind = clang_getCursorKind(Cursor);
		eastl::string CursorName = ClangUtils::GetCursorDisplayName(Cursor);

		switch (CursorKind)
		{
		case (CXCursor_MacroExpansion):
		{
			return Visitor::VisitMacro(Cursor, Parent, ParserContext);
		}

		case (CXCursor_InclusionDirective):
		{
			// Capture the include directive on the header it appears in so
			// the post-parse validation can enforce that reflection-bearing
			// headers include their generated companion file last. The
			// dispatcher already pinned ReflectedHeader to the file owning
			// this cursor.
			FIncludeRef IncludeRef;
			IncludeRef.Spelling = CursorName;
			IncludeRef.Basename = CursorName;
			eastl::replace(IncludeRef.Basename.begin(), IncludeRef.Basename.end(), '\\', '/');
			const size_t SlashIdx = IncludeRef.Basename.find_last_of('/');
			if (SlashIdx != eastl::string::npos)
			{
				IncludeRef.Basename.erase(0, SlashIdx + 1);
			}
			IncludeRef.Basename.make_lower();

			uint32_t Line = 0;
			clang_getExpansionLocation(Loc, nullptr, &Line, nullptr, nullptr);
			IncludeRef.LineNumber = Line;

			ParserContext->ReflectedHeader->Includes.push_back(eastl::move(IncludeRef));
			return CXChildVisit_Continue;
		}

		case(CXCursor_ClassDecl):
		{
			ParserContext->PushNamespace(CursorName);
			clang_visitChildren(Cursor, VisitTranslationUnit, ClientData);
			ParserContext->PopNamespace();

			return Visitor::VisitClass(Cursor, Parent, ParserContext);
		}

		case(CXCursor_StructDecl):
		{
			ParserContext->PushNamespace(CursorName);
			clang_visitChildren(Cursor, VisitTranslationUnit, ClientData);
			ParserContext->PopNamespace();

			return Visitor::VisitStructure(Cursor, Parent, ParserContext);
		}

		case(CXCursor_EnumDecl):
		{
			return Visitor::VisitEnum(Cursor, Parent, ParserContext);
		}

		case(CXCursor_Namespace):
		{
			ParserContext->PushNamespace(CursorName);
			clang_visitChildren(Cursor, VisitTranslationUnit, ClientData);
			ParserContext->PopNamespace();
		}

		return CXChildVisit_Continue;

		default:
		{
			return CXChildVisit_Continue;
		}
		}
	}
}
