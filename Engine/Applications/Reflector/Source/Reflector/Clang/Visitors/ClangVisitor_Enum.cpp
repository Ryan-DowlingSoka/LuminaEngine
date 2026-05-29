#include "ClangVisitor.h"
#include "Reflector/Clang/ClangParserContext.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/Diagnostics/LRTDiagnostics.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina::Reflection::Visitor
{
    static CXChildVisitResult VisitEnumContents(CXCursor Cursor, CXCursor, CXClientData pClientData)
    {
        FClangParserContext* pContext = static_cast<FClangParserContext*>(pClientData);
        FReflectedEnum* Enum = (FReflectedEnum*)pContext->ParentReflectedType;
        
        CXCursorKind kind = clang_getCursorKind(Cursor);
        
        if (kind == CXCursor_EnumConstantDecl)
        {
            eastl::string DisplayName = ClangUtils::GetCursorDisplayName(Cursor);
            const clang::EnumConstantDecl* EnumConstantDecl = (const clang::EnumConstantDecl*)Cursor.data[0];

            const llvm::APSInt& initVal = EnumConstantDecl->getInitVal();
            uint32_t Value = (int32_t)initVal.getExtValue();
            
            FReflectedEnum::FConstant Constant;
            Constant.Label = DisplayName;
            Constant.ID = eastl::string(DisplayName);
            Constant.Value = Value;
            
            const CXString CommentString = clang_Cursor_getBriefCommentText(Cursor);
            if (CommentString.data != nullptr)
            {
                Constant.Description = clang_getCString(CommentString);
            }
            clang_disposeString(CommentString);

            Enum->AddConstant(Constant);
        }

        return CXChildVisit_Continue;
    }
    
    CXChildVisitResult VisitEnum(CXCursor Cursor, CXCursor, FClangParserContext* Context)
    {
        eastl::string CursorName = ClangUtils::GetCursorDisplayName(Cursor);

        void* Data = clang_getCursorType(Cursor).data[0];
        if(Data == nullptr)
        {
            return CXChildVisit_Break;
        }
        
        FReflectionMacro Macro;
        if(!Context->TryFindMacroForCursor(Context->ReflectedHeader->HeaderPath, Cursor, Macro))
        {
            return CXChildVisit_Continue;
        }

        
        eastl::string FullyQualifiedName;
        if (!ClangUtils::GetQualifiedNameForType(clang::QualType::getFromOpaquePtr(Data), FullyQualifiedName))
        {
            return CXChildVisit_Break;
        }
        
        if(Macro.Type != EReflectionMacro::Reflect)
        {
            return CXChildVisit_Continue;
        }

        // Naming convention: reflected enums are prefixed with `E`.
        if (CursorName.empty() || CursorName[0] != 'E')
        {
            LRT_WARNING(Cursor, EDiagId::BadTypePrefix,
                "Reflected enum '%s' should be prefixed with 'E' (e.g. 'E%s').",
                CursorName.c_str(), CursorName.c_str());
        }

        const clang::EnumDecl* pEnumDecl = (const clang::EnumDecl*) Cursor.data[0];
        clang::QualType IntegerType = pEnumDecl->getIntegerType();

        if (IntegerType.isNull())
        {
            return CXChildVisit_Break;
        }
        
        FReflectedEnum* ReflectedEnum = Context->ReflectionDatabase.GetOrCreateReflectedType<FReflectedEnum>(FStringHash(FullyQualifiedName));
        ReflectedEnum->DisplayName = CursorName;
        ReflectedEnum->Header = Context->ReflectedHeader;
        ReflectedEnum->Type = FReflectedType::EType::Enum;
        ReflectedEnum->LineNumber = ClangUtils::GetCursorLineNumber(Cursor);
        ReflectedEnum->GenerateMetadata(Macro.MacroContents);

        if (!Context->CurrentNamespace.empty())
        {
            ReflectedEnum->Namespace = Context->CurrentNamespace;
        }
        
        FReflectedType* PreviousParentType = Context->ParentReflectedType;
        
        Context->ParentReflectedType = ReflectedEnum;
        clang_visitChildren(Cursor, VisitEnumContents, Context);
        Context->ParentReflectedType = PreviousParentType;
        
        
        Context->ReflectionDatabase.AddReflectedType(ReflectedEnum);
        
        return CXChildVisit_Continue;

    }
}
