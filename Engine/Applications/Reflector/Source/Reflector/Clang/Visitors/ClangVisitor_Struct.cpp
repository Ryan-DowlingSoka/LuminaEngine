#include <clang/AST/Type.h>
#include <clang-c/CXString.h>
#include <clang-c/Index.h>
#include <EASTL/optional.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <string>
#include <Reflector/Types/ReflectedType.h>
#include <spdlog/spdlog.h>
#include "Reflector/Clang/ClangParserContext.h"
#include "Reflector/Clang/Utils.h"
#include "Reflector/ReflectionCore/ReflectionMacro.h"
#include "Reflector/Types/Functions/ReflectedFunction.h"
#include "Reflector/Types/Properties/ReflectedArrayProperty.h"
#include "Reflector/Types/Properties/ReflectedEnumProperty.h"
#include "Reflector/Types/Properties/ReflectedNumericProperty.h"
#include "Reflector/Types/Properties/ReflectedObjectProperty.h"
#include "Reflector/Types/Properties/ReflectedStringProperty.h"
#include "Reflector/Types/Properties/ReflectedStructProperty.h"

namespace Lumina::Reflection::Visitor
{
	// Extract the brief doc comment above a cursor, escaping characters that would
	// break a C string literal in the generated output (\, ").
	static eastl::string GetCursorComment(const CXCursor& Cursor)
	{
		const CXString CommentString = clang_Cursor_getBriefCommentText(Cursor);
		eastl::string Result;
		if (CommentString.data != nullptr)
		{
			const char* Raw = clang_getCString(CommentString);
			if (Raw && Raw[0] != '\0')
			{
				for (const char* P = Raw; *P; ++P)
				{
					if (*P == '\\')
					{
						Result += "\\\\";
					}
					else if (*P == '"')
					{
						Result += "\\\"";
					}
					else
					{
						Result += *P;
					}
				}
			}
		}
		clang_disposeString(CommentString);
		return Result;
	}

	static eastl::optional<FFieldInfo> CreateFieldInfo(FClangParserContext* Context, const CXCursor& Cursor)
	{
		eastl::string CursorName = ClangUtils::GetCursorDisplayName(Cursor);

		CXType FieldType = clang_getCursorType(Cursor);
		clang::QualType FieldQualType = ClangUtils::GetQualType(FieldType);
		if (FieldQualType.isNull())
		{
			spdlog::error("Failed to qualify typename for member: {} in class: {}", CursorName.c_str(), Context->ParentReflectedType->GetTypeName().c_str());
			return eastl::nullopt;
		}

		eastl::string TypeSpelling;
		ClangUtils::GetQualifiedNameForType(FieldQualType, TypeSpelling);
		EPropertyTypeFlags PropFlags = GetCoreTypeFromName(TypeSpelling.c_str());
		
		
		// Is not a core type.
		if (PropFlags == EPropertyTypeFlags::None)
		{
			if (FieldQualType->isEnumeralType())
			{
				PropFlags = EPropertyTypeFlags::Enum;
			}
			else if (FieldQualType->isStructureType())
			{
				PropFlags = EPropertyTypeFlags::Struct;
			}
			else if (FieldQualType->isPointerType())
			{
				spdlog::error("{}, Object properties as pointer are not supported, use TObjectPtr instead", CursorName.c_str());
				return eastl::nullopt;
			}
		}
		
		FFieldInfo Info;
		
		if (clang_isConstQualifiedType(FieldType))
		{
			Info.PropertyFlags |= EPropertyFlags::Const;
		}

		switch (clang_getCXXAccessSpecifier(Cursor))
		{
			case CX_CXXPrivate:   Info.PropertyFlags |= EPropertyFlags::Private;   break;
			case CX_CXXProtected: Info.PropertyFlags |= EPropertyFlags::Protected; break;
			default: break;
		}
		
		if (clang_isPODType(FieldType))
		{
			Info.PropertyFlags |= EPropertyFlags::Trivial;
		}
		
		if (FieldType.kind >= CXType_FirstBuiltin && FieldType.kind <= CXType_LastBuiltin)
		{
			Info.PropertyFlags |= EPropertyFlags::Builtin;
		}
		
		Info.Flags			= PropFlags;
		Info.Type			= FieldType;
		Info.Name			= CursorName;
		Info.TypeName		= TypeSpelling;
		Info.RawFieldType	= FieldQualType.getAsString().c_str();

		return Info;
	}
	
	static eastl::optional<FFieldInfo> CreateFuncField(FClangParserContext* Context, const CXType& FieldType)
	{
		clang::QualType FieldQualType = ClangUtils::GetQualType(FieldType);
		if (FieldQualType.isNull())
		{
			return eastl::nullopt;
		}

		eastl::string TypeSpelling;
		ClangUtils::GetQualifiedNameForType(FieldQualType, TypeSpelling);
		EPropertyTypeFlags PropFlags = GetCoreTypeFromName(TypeSpelling.c_str());
		
		// Is not a core type.
		if (PropFlags == EPropertyTypeFlags::None)
		{
			if (FieldQualType->isEnumeralType())
			{
				PropFlags = EPropertyTypeFlags::Enum;
			}
			else if (FieldQualType->isStructureType())
			{
				PropFlags = EPropertyTypeFlags::Struct;
			}
			else if (FieldQualType->isPointerType())
			{
				PropFlags = EPropertyTypeFlags::Object;
			}
			else if (FieldQualType->isReferenceType())
			{
				clang::QualType ReferenceType = FieldQualType->getAs<clang::ReferenceType>()->getPointeeType();
				if (ReferenceType->isStructureType())
				{
					PropFlags = EPropertyTypeFlags::Struct;
				}
				else
				{
					ClangUtils::GetQualifiedNameForType(ReferenceType, TypeSpelling);
					PropFlags = GetCoreTypeFromName(TypeSpelling.c_str());
				}
			}
		}
		
		FFieldInfo Info;
		
		if (clang_isConstQualifiedType(FieldType))
		{
			Info.PropertyFlags |= EPropertyFlags::Const;
		}
		
		if (clang_isPODType(FieldType))
		{
			Info.PropertyFlags |= EPropertyFlags::Trivial;
		}
		
		if (FieldType.kind >= CXType_FirstBuiltin && FieldType.kind <= CXType_LastBuiltin)
		{
			Info.PropertyFlags |= EPropertyFlags::Builtin;
		}
		
		Info.Flags			= PropFlags;
		Info.Type			= FieldType;
		Info.Name			= "None";
		Info.TypeName		= TypeSpelling;
		Info.RawFieldType	= FieldQualType.getAsString().c_str();

		return Info;
	}

	static eastl::optional<FFieldInfo> CreateSubFieldInfo(FClangParserContext* Context, const CXType& FieldType, const FFieldInfo& ParentField)
	{
		clang::QualType FieldQualType = ClangUtils::GetQualType(FieldType);
		if (FieldQualType.isNull())
		{
			spdlog::error("Failed to qualify typename for member: {} in class: {}", ParentField.Name.c_str(), Context->ParentReflectedType->GetTypeName().c_str());
			return eastl::nullopt;
		}

		eastl::string FieldName;
		ClangUtils::GetQualifiedNameForType(FieldQualType, FieldName);

		EPropertyTypeFlags PropFlags = GetCoreTypeFromName(FieldName.c_str());

		// Is not a core type.
		if (PropFlags == EPropertyTypeFlags::None)
		{
			if (FieldQualType->isEnumeralType())
			{
				PropFlags = EPropertyTypeFlags::Enum;
			}
			else if (FieldQualType->isStructureType())
			{
				PropFlags = EPropertyTypeFlags::Struct;
			}
			else if (FieldQualType->isPointerType())
			{
				spdlog::error("[{}] Object properties as pointers are *NOT* supported, use TObjectPtr instead", FieldName.c_str());
				return eastl::nullopt;
			}
		}

		FFieldInfo Info;
		
		if (clang_isConstQualifiedType(FieldType))
		{
			Info.PropertyFlags |= EPropertyFlags::Const;
		}
		
		if (clang_isPODType(FieldType))
		{
			Info.PropertyFlags |= EPropertyFlags::Trivial;
		}
		
		if (FieldType.kind >= CXType_FirstBuiltin && FieldType.kind <= CXType_LastBuiltin)
		{
			Info.PropertyFlags |= EPropertyFlags::Builtin;
		}
		
		Info.Flags			= PropFlags;
		Info.Type			= FieldType;
		Info.TypeName		= FieldName;
		Info.RawFieldType	= ParentField.RawFieldType;
		return Info;
	}

	template<std::derived_from<FReflectedProperty> T>
	static eastl::unique_ptr<T> CreateProperty(const FFieldInfo& Info)
	{
		eastl::unique_ptr<T> New = eastl::make_unique<T>();
		New->Name			= Info.Name;
		New->TypeName		= Info.TypeName;
		New->RawTypeName	= Info.RawFieldType;
		New->PropertyFlags	= Info.PropertyFlags;
		return New;
	}

	static bool CreatePropertyForType(FClangParserContext* Context, FReflectedStruct* Struct, FReflectedProperty*& OutProperty, const FFieldInfo& FieldInfo)
	{
		OutProperty = nullptr;
		
		eastl::unique_ptr<FReflectedProperty> NewProperty;
		switch (FieldInfo.Flags)
		{
		case EPropertyTypeFlags::UInt8:
		{
			NewProperty = CreateProperty<FReflectedUInt8Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::UInt16:
		{
			NewProperty = CreateProperty<FReflectedUInt16Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::UInt32:
		{
			NewProperty = CreateProperty<FReflectedUInt32Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::UInt64:
		{
			NewProperty = CreateProperty<FReflectedUInt64Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Int8:
		{
			NewProperty = CreateProperty<FReflectedInt8Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Int16:
		{
			NewProperty = CreateProperty<FReflectedInt16Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Int32:
		{
			NewProperty = CreateProperty<FReflectedInt32Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Int64:
		{
			NewProperty = CreateProperty<FReflectedInt64Property>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Float:
		{
			NewProperty = CreateProperty<FReflectedFloatProperty>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Double:
		{
			NewProperty = CreateProperty<FReflectedDoubleProperty>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Bool:
		{
			NewProperty = CreateProperty<FReflectedBoolProperty>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::String:
		{
			NewProperty = CreateProperty<FReflectedStringProperty>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Name:
		{
			NewProperty = CreateProperty<FReflectedNameProperty>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Struct:
		{
			NewProperty = CreateProperty<FReflectedStructProperty>(FieldInfo);
		}
		break;
		case EPropertyTypeFlags::Enum:
		{
			NewProperty = CreateProperty<FReflectedEnumProperty>(FieldInfo);
			const CXCursor EnumCursor = clang_getTypeDeclaration(FieldInfo.Type);

			if (clang_getCursorKind(EnumCursor) == CXCursor_EnumDecl)
			{
				CXType UnderlyingType = clang_getEnumDeclIntegerType(EnumCursor);
				eastl::optional<FFieldInfo> SubType = CreateSubFieldInfo(Context, UnderlyingType, FieldInfo);
				if (!SubType.has_value())
				{
					return false;
				}

				SubType->Name = FieldInfo.Name + "_Inner";
				SubType->PropertyFlags |= EPropertyFlags::SubField;

				FReflectedProperty* FieldProperty;
				CreatePropertyForType(Context, Struct, FieldProperty, SubType.value());
				FieldProperty->bInner = true;
			}
		}
		break;
		case EPropertyTypeFlags::Object:
		{
			const CXType ArgType = clang_Type_getTemplateArgumentAsType(FieldInfo.Type, 0);
			eastl::optional<FFieldInfo> ParamFieldInfo = CreateSubFieldInfo(Context, ArgType, FieldInfo);
			if (!ParamFieldInfo.has_value())
			{
				return false;
			}

			ParamFieldInfo->Name = FieldInfo.Name; // Replace the empty template property name with the parent.

			NewProperty = CreateProperty<FReflectedObjectProperty>(ParamFieldInfo.value());
		}
		break;
		case EPropertyTypeFlags::Vector:
		{
			auto ArrayProperty = CreateProperty<FReflectedArrayProperty>(FieldInfo);

			const CXType ArgType = clang_Type_getTemplateArgumentAsType(FieldInfo.Type, 0);
			eastl::optional<FFieldInfo> ParamFieldInfo = CreateSubFieldInfo(Context, ArgType, FieldInfo);
			if (!ParamFieldInfo.has_value())
			{
				return false;
			}

			ParamFieldInfo->Name = FieldInfo.Name + "_Inner";
			ParamFieldInfo->PropertyFlags |= EPropertyFlags::SubField;

			FReflectedProperty* FieldProperty;
			CreatePropertyForType(Context, Struct, FieldProperty, ParamFieldInfo.value());
			if (FieldProperty == nullptr)
			{
				spdlog::error("Failed to create property for array. {} [{}]", FieldInfo.Name.c_str(), ParamFieldInfo->Name.c_str());
				return false;
			}

			ArrayProperty->ElementTypeName = clang_getCString(clang_getTypeSpelling(ArgType));
			NewProperty = eastl::move(ArrayProperty);

			FieldProperty->bInner = true; // This property "belongs" to the array.
		}
		break;
		default:
		{

		}
		break;
		}

		if (NewProperty != nullptr)
		{
			OutProperty = NewProperty.get();
			Struct->PushProperty(eastl::move(NewProperty));
		}

		return NewProperty != nullptr;
	}


	static bool CreateFunctionForType(const CXCursor& Cursor, FClangParserContext* Context, FReflectedStruct* Struct, FReflectedFunction*& OutFunction)
	{
		OutFunction = nullptr;
		
		auto NewFunction = eastl::make_unique<FReflectedFunction>();
		NewFunction->Outer = Struct->DisplayName;
		NewFunction->Name = ClangUtils::GetCursorSpelling(Cursor);

		int NumArgs = clang_Cursor_getNumArguments(Cursor);

		for (int i = 0; i < NumArgs; ++i)
		{
			CXCursor ArgCursor = clang_Cursor_getArgument(Cursor, i);
			eastl::string ArgName = ClangUtils::GetCursorSpelling(ArgCursor);
			CXType FieldType = clang_getCursorType(ArgCursor);
			auto Field = CreateFuncField(Context, FieldType);
			if (Field.has_value() && Field->Flags != EPropertyTypeFlags::None)
			{
				Field->Name		= eastl::move(ArgName);
				NewFunction->AddArgument(eastl::move(Field.value()));
			}
			else
			{
				spdlog::error("Failed to create function field for {}", NewFunction->Name.c_str());
			}
		}
		
		CXType FuncType = clang_getCursorType(Cursor);
		CXType ResultType = clang_getResultType(FuncType);
		
		if (ResultType.kind != CXType_Void)
		{
			NewFunction->Return = CreateFuncField(Context, ResultType);
		}
		
		OutFunction = NewFunction.get();
		Struct->PushFunction(eastl::move(NewFunction));

		return NewFunction != nullptr;
	}

	template<typename TVisitType>
	static CXChildVisitResult VisitContents(CXCursor Cursor, CXCursor Parent, CXClientData pClientData)
	{
		FClangParserContext* Context = (FClangParserContext*)pClientData;
		eastl::string CursorName = ClangUtils::GetCursorDisplayName(Cursor);
		CXCursorKind Kind = clang_getCursorKind(Cursor);
		TVisitType* Type = Context->GetParentReflectedType<TVisitType>();
		
		switch (Kind)
		{
		case(CXCursor_CXXBaseSpecifier):
		{
			if (Type->Parent.empty())
			{
				Type->Parent = CursorName;
			}
		}
		break;
		case(CXCursor_FieldDecl):
		{
			FReflectionMacro Macro;
			if (!Context->TryFindMacroForCursor(Context->ReflectedHeader->HeaderPath, Cursor, Macro))
			{
				return CXChildVisit_Continue;
			}

			eastl::optional<FFieldInfo> FieldInfo = CreateFieldInfo(Context, Cursor);
			if (!FieldInfo.has_value())
			{
				return CXChildVisit_Continue;
			}

			FReflectedProperty* NewProperty;
			CreatePropertyForType(Context, Type, NewProperty, FieldInfo.value());
			NewProperty->GenerateMetadata(Macro.MacroContents);

			eastl::string Comment = GetCursorComment(Cursor);
			if (!Comment.empty())
			{
				NewProperty->Metadata.push_back({"ToolTip", eastl::move(Comment)});
			}
		}
		break;
		case(CXCursor_CXXMethod):
		{
			FReflectionMacro Macro;
			if (!Context->TryFindMacroForCursor(Context->ReflectedHeader->HeaderPath, Cursor, Macro))
			{
				return CXChildVisit_Continue;
			}

			FReflectedFunction* NewFunction;
			CreateFunctionForType(Cursor, Context, Type, NewFunction);
			NewFunction->GenerateMetadata(Macro.MacroContents);

			eastl::string Comment = GetCursorComment(Cursor);
			if (!Comment.empty())
			{
				NewFunction->Metadata.push_back({"ToolTip", eastl::move(Comment)});
			}
		}
		break;
		}

		return CXChildVisit_Continue;

	}

	CXChildVisitResult VisitStructure(CXCursor Cursor, CXCursor Parent, FClangParserContext* Context)
	{
		eastl::string CursorName = ClangUtils::GetCursorDisplayName(Cursor);

		eastl::string FullyQualifiedCursorName;
		CXType Type = clang_getCursorType(Cursor);
		void* Data = Type.data[0];

		if (!ClangUtils::GetQualifiedNameForType(clang::QualType::getFromOpaquePtr(Data), FullyQualifiedCursorName))
		{
			return CXChildVisit_Break;
		}

		FReflectionMacro Macro;
		if (!Context->TryFindMacroForCursor(Context->ReflectedHeader->HeaderPath, Cursor, Macro))
		{
			return CXChildVisit_Continue;
		}

		FReflectionMacro GeneratedBody;
		if (!Context->TryFindGeneratedBodyMacro(Context->ReflectedHeader->HeaderPath, Cursor, GeneratedBody))
		{
			return CXChildVisit_Break;
		}

		FReflectedStruct* ReflectedStruct = Context->ReflectionDatabase.GetOrCreateReflectedType<FReflectedStruct>(FStringHash(FullyQualifiedCursorName));
		ReflectedStruct->DisplayName = CursorName;
		ReflectedStruct->GenerateMetadata(Macro.MacroContents);
		ReflectedStruct->Header = Context->ReflectedHeader;
		ReflectedStruct->Type = FReflectedType::EType::Structure;
		ReflectedStruct->GeneratedBodyLineNumber = GeneratedBody.LineNumber;
		ReflectedStruct->LineNumber = ClangUtils::GetCursorLineNumber(Cursor);
		ReflectedStruct->GenerateMetadata(Macro.MacroContents);

		if (!Context->CurrentNamespace.empty())
		{
			ReflectedStruct->Namespace = Context->CurrentNamespace;
		}

		eastl::string StructComment = GetCursorComment(Cursor);
		if (!StructComment.empty())
		{
			ReflectedStruct->Metadata.push_back({"ToolTip", eastl::move(StructComment)});
		}

		FReflectedType* PreviousType = Context->ParentReflectedType;
		Context->ParentReflectedType = ReflectedStruct;
		Context->LastReflectedType = ReflectedStruct;

		clang_visitChildren(Cursor, VisitContents<FReflectedStruct>, Context);

		Context->ParentReflectedType = PreviousType;
		Context->ReflectionDatabase.AddReflectedType(ReflectedStruct);

		return CXChildVisit_Recurse;
	}

	CXChildVisitResult VisitClass(CXCursor Cursor, CXCursor Parent, FClangParserContext* Context)
	{
		eastl::string CursorName = ClangUtils::GetCursorDisplayName(Cursor);

		eastl::string FullyQualifiedCursorName;
		CXType Type = clang_getCursorType(Cursor);
		void* Data = Type.data[0];

		if (!ClangUtils::GetQualifiedNameForType(clang::QualType::getFromOpaquePtr(Data), FullyQualifiedCursorName))
		{
			return CXChildVisit_Break;
		}

		FReflectionMacro Macro;
		if (!Context->TryFindMacroForCursor(Context->ReflectedHeader->HeaderPath, Cursor, Macro))
		{
			return CXChildVisit_Continue;
		}

		FReflectionMacro GeneratedBody;
		if (!Context->TryFindGeneratedBodyMacro(Context->ReflectedHeader->HeaderPath, Cursor, GeneratedBody))
		{
			return CXChildVisit_Break;
		}

		FReflectedClass* ReflectedClass = Context->ReflectionDatabase.GetOrCreateReflectedType<FReflectedClass>(FStringHash(FullyQualifiedCursorName));
		ReflectedClass->DisplayName = CursorName;
		ReflectedClass->Header = Context->ReflectedHeader;
		ReflectedClass->Type = FReflectedType::EType::Class;
		ReflectedClass->GeneratedBodyLineNumber = GeneratedBody.LineNumber;
		ReflectedClass->LineNumber = ClangUtils::GetCursorLineNumber(Cursor);
		ReflectedClass->GenerateMetadata(Macro.MacroContents);

		if (!Context->CurrentNamespace.empty())
		{
			ReflectedClass->Namespace = Context->CurrentNamespace;
		}

		eastl::string ClassComment = GetCursorComment(Cursor);
		if (!ClassComment.empty())
		{
			ReflectedClass->Metadata.push_back({"ToolTip", eastl::move(ClassComment)});
		}

		FReflectedType* PreviousType = Context->ParentReflectedType;
		Context->ParentReflectedType = ReflectedClass;
		Context->LastReflectedType = ReflectedClass;

		clang_visitChildren(Cursor, VisitContents<FReflectedClass>, Context);

		Context->ParentReflectedType = PreviousType;
		Context->ReflectionDatabase.AddReflectedType(ReflectedClass);

		return CXChildVisit_Recurse;
	}
}
