#include "MaterialCompiler.h"

#include "Assets/AssetTypes/Textures/Texture.h"
#include "Nodes/MaterialNodeExpression.h"
#include "Nodes/MaterialNode_PrimitiveData.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "UI/Tools/NodeGraph/EdGraphNode.h"

namespace Lumina
{
	FMaterialCompiler::FMaterialCompiler()
	{
		ShaderChunks.reserve(2000);
	}

	FString FMaterialCompiler::BuildTree(size_t& StartReplacement, size_t& EndReplacement, EMaterialType MaterialType) const
	{
		const FString BasePath = Paths::GetEngineResourceDirectory() + "/Shaders/MaterialShader/";
		const FString FragmentPath = (MaterialType == EMaterialType::Terrain)
			? BasePath + "TerrainBasePixelPass.slang"
			: BasePath + "BasePixelPass.slang";

		FString LoadedString;
		if (!FileHelper::LoadFileIntoString(LoadedString, FragmentPath))
		{
			LOG_ERROR("Failed to find {}!", FragmentPath);
			return LoadedString;
		}

		const char* Token = "$MATERIAL_INPUTS";
		size_t Pos = LoadedString.find(Token);

		if (Pos != FString::npos)
		{
			StartReplacement = Pos;
			LoadedString.replace(Pos, strlen(Token), ShaderChunks);
			EndReplacement = Pos + ShaderChunks.length();
		}
		else
		{
			LOG_ERROR("Missing [$MATERIAL_INPUTS] in base shader!");
			return LoadedString;
		}

		return LoadedString;
	}

	static FString GetVectorType(EMaterialInputType Type)
	{
		switch (Type)
		{
			case EMaterialInputType::Float:		return "float";
			case EMaterialInputType::Float2:	return "float2";
			case EMaterialInputType::Float3:	return "float3";
			case EMaterialInputType::Float4:	return "float4";
			case EMaterialInputType::Texture: return "float4";
			default: return "float";
		}
	}

	static FString GetVectorType(int32 ComponentCount)
	{
		switch (ComponentCount)
		{
			case 1: return "float";
			case 2: return "float2";
			case 3: return "float3";
			case 4: return "float4";
			default: return "float";
		}
	}

	int32 FMaterialCompiler::GetComponentCount(EComponentMask Mask)
	{
		switch (Mask)
		{
			case EComponentMask::None: return 0;
			case EComponentMask::RGBA: return 4;
			case EComponentMask::R: return 1;
			case EComponentMask::G: return 1;
			case EComponentMask::B: return 1;
			case EComponentMask::A: return 1;
			case EComponentMask::RG: return 2;
			case EComponentMask::GB: return 2;
			case EComponentMask::RGB: return 3;
		}
		
		return 0;
	}

	int32 FMaterialCompiler::GetComponentCount(EMaterialInputType Type)
	{
		switch (Type)
		{
			case EMaterialInputType::Float:		return 1;
			case EMaterialInputType::Float2:	return 2;
			case EMaterialInputType::Float3:	return 3;
			case EMaterialInputType::Float4:	return 4;
			case EMaterialInputType::Texture:	return 4;
			default: return 1;
		}
	}

	static EMaterialInputType GetTypeFromComponentCount(int32 Count)
	{
		switch (Count)
		{
		case 1: return EMaterialInputType::Float;
		case 2: return EMaterialInputType::Float2;
		case 3: return EMaterialInputType::Float3;
		case 4: return EMaterialInputType::Float4;
		default: return EMaterialInputType::Float;
		}
	}
	
	FMaterialCompiler::FInputValue FMaterialCompiler::GetTypedInputValue(CMaterialInput* Input, float DefaultValue)
	{
		return GetTypedInputValue(Input, eastl::to_string(DefaultValue));
	}

	FMaterialCompiler::FInputValue FMaterialCompiler::GetTypedInputValue(CMaterialInput* Input, const FString& DefaultValueStr)
	{
		FInputValue Result;

		if (Input->HasConnection())
		{
			CMaterialOutput* Conn	= Input->GetConnection<CMaterialOutput>(0);
			FString NodeName		= Conn->GetOwningNode()->GetNodeFullName();
			
			Result.Type				= Conn->InputType;
			Result.ComponentCount	= GetComponentCount(Result.Type);
			Result.Value 			= NodeName;
			Result.Mask  			= Conn->GetComponentMask();
		}
		else
		{
			FString NodeName		= Input->GetOwningNode()->GetNodeFullName();

			Result.Type				= GetTypeFromComponentCount(GetComponentCount(Input->GetComponentMask()));
			Result.ComponentCount	= GetComponentCount(Input->GetComponentMask());
			Result.Value 			= DefaultValueStr;
			Result.Mask  			= Input->GetComponentMask();
		}

		return Result;
	}

	EMaterialInputType FMaterialCompiler::DetermineResultType(EMaterialInputType A, EMaterialInputType B, bool IsComponentWise)
	{
		int32 CountA = GetComponentCount(A);
		int32 CountB = GetComponentCount(B);

		if (IsComponentWise)
		{
			if (CountA == 1)
			{
				return B;
			}
			if (CountB == 1)
			{
				return A;
			}
			
			return CountA >= CountB ? A : B;
		}
		else
		{
			return EMaterialInputType::Float;
		}
	}

	EMaterialInputType FMaterialCompiler::EmitBinaryOp(const FString& Op, CMaterialInput* A, CMaterialInput* B, float DefaultA, float DefaultB, bool IsComponentWise)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue AValue = GetTypedInputValue(A, DefaultA);
		FInputValue BValue = GetTypedInputValue(B, DefaultB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, IsComponentWise);
		FString ResultTypeStr = GetVectorType(ResultType);

		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform " + Op + " between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		FString RMask = GetSwizzleForMask(AValue.Mask);
		FString GMask = GetSwizzleForMask(BValue.Mask);
		
		ShaderChunks.append(ResultTypeStr + " " + OwningNode + " = " + AValue.Value + RMask + " " + Op + " " + BValue.Value + GMask + ";\n");
		
		return ResultType;
	}
	
	void FMaterialCompiler::DefineFloatParameter(const FString& NodeID, const FName& ParamID, float Value)
	{
		if (ScalarParameters.find(ParamID) == ScalarParameters.end())
		{
			ScalarParameters[ParamID].Index = NumScalarParams++;
			ScalarParameters[ParamID].Value = Value;
		}

		FString IndexString = eastl::to_string(ScalarParameters[ParamID].Index);
		ShaderChunks.append("float " + NodeID + " = GetMaterialScalar(" + IndexString + ");\n");
	}

	void FMaterialCompiler::DefineFloat2Parameter(const FString& NodeID, const FName& ParamID, float Value[2])
	{
		if (VectorParameters.find(ParamID) == VectorParameters.end())
		{
			VectorParameters[ParamID].Index = NumVectorParams++;
			VectorParameters[ParamID].Value = glm::vec4(Value[0], Value[1], 0.0f, 1.0f);
		}

		FString IndexString = eastl::to_string(VectorParameters[ParamID].Index);
		ShaderChunks.append("float2 " + NodeID + " = GetMaterialVec4(" + IndexString + ").xy;\n");
	}

	void FMaterialCompiler::DefineFloat3Parameter(const FString& NodeID, const FName& ParamID, float Value[3])
	{
		if (VectorParameters.find(ParamID) == VectorParameters.end())
		{
			VectorParameters[ParamID].Index = NumVectorParams++;
			VectorParameters[ParamID].Value = glm::vec4(Value[0], Value[1], Value[2], 1.0f);
		}

		FString IndexString = eastl::to_string(VectorParameters[ParamID].Index);
		ShaderChunks.append("float3 " + NodeID + " = GetMaterialVec4(" + IndexString + ").xyz;\n");
	}

	void FMaterialCompiler::DefineFloat4Parameter(const FString& NodeID, const FName& ParamID, float Value[4])
	{
		if (VectorParameters.find(ParamID) == VectorParameters.end())
		{
			VectorParameters[ParamID].Index = NumVectorParams++;
			VectorParameters[ParamID].Value = glm::vec4(Value[0], Value[1], Value[2], Value[3]);
		}

		FString IndexString = eastl::to_string(VectorParameters[ParamID].Index);
		ShaderChunks.append("float4 " + NodeID + " = GetMaterialVec4(" + IndexString + ");\n");
	}

	void FMaterialCompiler::DefineConstantFloat(const FString& ID, float Value)
	{
		FString ValueString = eastl::to_string(Value);
		ShaderChunks.append("float " + ID + " = " + ValueString + ";\n");
	}

	void FMaterialCompiler::DefineConstantFloat2(const FString& ID, float Value[2])
	{
		FString ValueStringX = eastl::to_string(Value[0]);
		FString ValueStringY = eastl::to_string(Value[1]);
		ShaderChunks.append("float2 " + ID + " = float2(" + ValueStringX + ", " + ValueStringY + ");\n");
	}

	void FMaterialCompiler::DefineConstantFloat3(const FString& ID, float Value[3])
	{
		FString ValueStringX = eastl::to_string(Value[0]);
		FString ValueStringY = eastl::to_string(Value[1]);
		FString ValueStringZ = eastl::to_string(Value[2]);
		ShaderChunks.append("float3 " + ID + " = float3(" + ValueStringX + ", " + ValueStringY + ", " + ValueStringZ + ");\n");
	}

	void FMaterialCompiler::DefineConstantFloat4(const FString& ID, float Value[4])
	{
		FString ValueStringX = eastl::to_string(Value[0]);
		FString ValueStringY = eastl::to_string(Value[1]);
		FString ValueStringZ = eastl::to_string(Value[2]);
		FString ValueStringW = eastl::to_string(Value[3]);
		ShaderChunks.append("float4 " + ID + " = float4(" + ValueStringX + ", " + ValueStringY + ", " + ValueStringZ + ", " + ValueStringW + ");\n");
	}

	void FMaterialCompiler::BreakFloat2(CMaterialInput* A)
	{
		const FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue ValueString = GetTypedInputValue(A, 0.0);
		
		const FString TypeStr = GetVectorType(EMaterialInputType::Float2);

		if (ValueString.ComponentCount != 2)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "BreakFloat2 requires a Float2 input, got " + GetVectorType(ValueString.Type);
			AddError(Error);
			ShaderChunks.append("// ERROR: Type mismatch in BreakFloat2\n");
		}
		
		ShaderChunks.append(TypeStr + " " + OwningNode + " = " + ValueString.Value + ".xy" + ";\n");
	}

	void FMaterialCompiler::BreakFloat3(CMaterialInput* A)
	{
		const FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue ValueString = GetTypedInputValue(A, 0.0);
		const FString TypeStr = GetVectorType(EMaterialInputType::Float3);
		
		if (ValueString.ComponentCount != 3)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "BreakFloat3 requires a Float3 input, got " + GetVectorType(ValueString.Type);
			AddError(Error);
			ShaderChunks.append("// ERROR: Type mismatch in BreakFloat3\n");
		}
		
		ShaderChunks.append(TypeStr + " " + OwningNode + " = " + ValueString.Value + ".xyz" + ";\n");
	}

	void FMaterialCompiler::BreakFloat4(CMaterialInput* A)
	{
		const FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue ValueString = GetTypedInputValue(A, 0.0);

		const FString TypeStr = GetVectorType(EMaterialInputType::Float4);
		
		if (ValueString.ComponentCount != 4)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "BreakFloat4 requires a Float4 input, got " + GetVectorType(ValueString.Type);
			AddError(Error);
			ShaderChunks.append("// ERROR: Type mismatch in BreakFloat4\n");
		}
		
		ShaderChunks.append(TypeStr + " " + OwningNode + " = " + ValueString.Value + ".xyzw" + ";\n");
	}

	void FMaterialCompiler::MakeFloat2(CMaterialInput* R, CMaterialInput* G)
	{
		const FString OwningNode = R->GetOwningNode()->GetNodeFullName();
		FInputValue ValueR = GetTypedInputValue(R, 0.0f);
		FInputValue ValueG = GetTypedInputValue(G, 0.0f);
		
		const FString TypeStr = GetVectorType(EMaterialInputType::Float2);
		
		
		if (ValueR.ComponentCount != 1 || ValueG.ComponentCount != 1)
		{
			EdNodeGraph::FError Error;
			Error.Node = R->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description.sprintf("MakeFloat2 requires two Float inputs, got %s and %s",
				GetVectorType(ValueR.Type).c_str(),
				GetVectorType(ValueG.Type).c_str());
		
			AddError(Error);
			ShaderChunks.append("// ERROR: Type mismatch in MakeFloat2\n");
			return;
		}
		
		FString RMask = GetSwizzleForMask(ValueR.Mask);
		FString GMask = GetSwizzleForMask(ValueG.Mask);
		
		ShaderChunks.append(TypeStr + " " + OwningNode + " = float2(" + ValueR.Value + RMask + ", " +  ValueG.Value + GMask + ");\n");
	}

	void FMaterialCompiler::MakeFloat3(CMaterialInput* R, CMaterialInput* G, CMaterialInput* B)
	{
		const FString OwningNode = R->GetOwningNode()->GetNodeFullName();
		FInputValue ValueR = GetTypedInputValue(R, 0.0f);
		FInputValue ValueG = GetTypedInputValue(G, 0.0f);
		FInputValue ValueB = GetTypedInputValue(B, 0.0f);

		const FString TypeStr = GetVectorType(EMaterialInputType::Float3);
		
		if (ValueR.ComponentCount != 1 || ValueG.ComponentCount != 1 || ValueB.ComponentCount != 1)
		{
			EdNodeGraph::FError Error;
			Error.Node = R->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description.sprintf("MakeFloat3 requires three Float inputs, got %s, %s and %s",
				GetVectorType(ValueR.Type).c_str(),
				GetVectorType(ValueG.Type).c_str(),
				GetVectorType(ValueB.Type).c_str());
		
			AddError(Error);
			ShaderChunks.append("// ERROR: Type mismatch in MakeFloat3\n");
			return;
		}
		
		FString RMask = GetSwizzleForMask(ValueR.Mask);
		FString GMask = GetSwizzleForMask(ValueG.Mask);
		FString BMask = GetSwizzleForMask(ValueB.Mask);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = float3(" + ValueR.Value + RMask + ", " +  ValueG.Value + GMask + ", " + ValueB.Value + BMask + ");\n");
	}

	void FMaterialCompiler::MakeFloat4(CMaterialInput* R, CMaterialInput* G, CMaterialInput* B, CMaterialInput* A)
	{
		const FString OwningNode = R->GetOwningNode()->GetNodeFullName();
		FInputValue ValueR = GetTypedInputValue(R, 0.0f);
		FInputValue ValueG = GetTypedInputValue(G, 0.0f);
		FInputValue ValueB = GetTypedInputValue(B, 0.0f);
		FInputValue ValueA = GetTypedInputValue(A, 0.0f);

		const FString TypeStr = GetVectorType(EMaterialInputType::Float4);
		
		if (ValueR.ComponentCount != 1 || ValueG.ComponentCount != 1 || ValueB.ComponentCount != 1 || ValueA.ComponentCount != 1)
		{
			EdNodeGraph::FError Error;
			Error.Node = R->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description.sprintf("MakeFloat4 requires four Float inputs, got %s, %s, %s and %s",
				GetVectorType(ValueR.Type).c_str(),
				GetVectorType(ValueG.Type).c_str(),
				GetVectorType(ValueB.Type).c_str(),
				GetVectorType(ValueA.Type).c_str());
		
			AddError(Error);
			ShaderChunks.append("// ERROR: Type mismatch in MakeFloat4\n");
		}
		
		FString RMask = GetSwizzleForMask(ValueR.Mask);
		FString GMask = GetSwizzleForMask(ValueG.Mask);
		FString BMask = GetSwizzleForMask(ValueB.Mask);
		FString AMask = GetSwizzleForMask(ValueA.Mask);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = float4(" + ValueR.Value + RMask + ", "
			+  ValueG.Value + GMask + ", " + ValueB.Value + BMask + ", " + ValueA.Value + AMask + ");\n");
	}

	void FMaterialCompiler::ComponentMask(CMaterialInput* A)
	{
		CMaterialExpression_ComponentMask* OwningNode = A->GetOwningNode<CMaterialExpression_ComponentMask>();
		
		if (!A->HasConnection())
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Invalid Action";
			Error.Description.append("ComponentMask is required to have an input value");
		
			AddError(Error);
			ShaderChunks.append("// ERROR: Component Mask Issue\n");
		}
		
		FString Swizzle = ".";
		int32 ComponentCount = 0;
		if (OwningNode->R)
		{
			Swizzle += "r";
			ComponentCount++;
		}
		if (OwningNode->G)
		{
			Swizzle += "g";
			ComponentCount++;
		}
		if (OwningNode->B)
		{
			Swizzle += "b";
			ComponentCount++;
		}
		if (OwningNode->A)
		{
			Swizzle += "a";
			ComponentCount++;
		}
		
		FString VectorType = GetVectorType(ComponentCount);
		FInputValue Value = GetTypedInputValue(A, "");
		
		ShaderChunks.append(VectorType + " " + OwningNode->GetNodeFullName() + " = " + Value.Value + Swizzle + ";\n");
	}
	
	void FMaterialCompiler::DefineTextureSample(const FString& ID)
	{
		return;
	}

	void FMaterialCompiler::TextureSample(const FString& ID, CTexture* Texture, CMaterialInput* Input)
	{
		if (Texture == nullptr || Texture->TextureResource == nullptr || !Texture->TextureResource->RHIImage.IsValid())
		{
			return;
		}

		FInputValue UVValue = GetTypedInputValue(Input, "float2(UV0)");

		FString UVStr;
		if (UVValue.ComponentCount >= 2)
		{
			UVStr = UVValue.Value + ".xy";
		}
		else
		{
			UVStr = "float2(" + UVValue.Value + ")";
		}

		auto It = eastl::find(BoundImages.begin(), BoundImages.end(), Texture);
    
		int32 Index;
		if (It != BoundImages.end())
		{
			Index = (int32)eastl::distance(BoundImages.begin(), It);
		}
		else
		{
			Index = (int32)BoundImages.size();
			BoundImages.push_back(Texture);
			NumTextureParams++;
		}

		ShaderChunks.append("float4 " + ID + " = uGlobalTextures[GetMaterialTexture(MaterialIndex, " + eastl::to_string(Index) + ")].Sample(" + UVStr + ");\n");
	}

	void FMaterialCompiler::NewLine()
	{
		ShaderChunks.append("\n");
	}

	void FMaterialCompiler::VertexNormal(const FString& ID)
	{
		ShaderChunks.append("float3 " + ID + " = WorldNormal.xyz;\n");
	}

	void FMaterialCompiler::TexCoords(const FString& ID, uint32 Index, float UTiling, float VTiling)
	{
		ShaderChunks.append("float2 " + ID + " = UV0 * float2(" + eastl::to_string(UTiling) + ", " + eastl::to_string(VTiling) + ");\n");
	}

	void FMaterialCompiler::Panner(CMaterialInput* UV, CMaterialInput* Time, CMaterialInput* Speed)
	{
		CMaterialExpression_Panner* PannerNode = UV->GetOwningNode<CMaterialExpression_Panner>();
		
		FInputValue UVValue = GetTypedInputValue(UV, "float2(UV0)");
		FInputValue TimeValue = GetTypedInputValue(Time, "GetTime()");
		FInputValue SpeedValue = GetTypedInputValue(Speed, "float2(" + eastl::to_string(PannerNode->SpeedX) + ", " + eastl::to_string(PannerNode->SpeedY) + ")");
		const FString OwningNode = UV->GetOwningNode()->GetNodeFullName();
		
		ShaderChunks.append("float2 " + OwningNode + " = " + UVValue.Value + " + " + SpeedValue.Value + " * " + TimeValue.Value + ";\n");
		
		PannerNode->Output->SetInputType(EMaterialInputType::Float2);
	}

	void FMaterialCompiler::WorldPos(const FString& ID)
	{
		ShaderChunks.append("float3 " + ID + " = WorldPosition;\n");
	}

	void FMaterialCompiler::CameraPos(const FString& ID)
	{
		ShaderChunks.append("float3 " + ID + " = GetCameraPosition();\n");
	}

	void FMaterialCompiler::EntityID(const FString& ID)
	{
		ShaderChunks.append("float " + ID + " = float(EntityID);\n");
	}

	void FMaterialCompiler::Time(const FString& ID)
	{
		ShaderChunks.append("float " + ID + " = GetTime();\n");
	}

	void FMaterialCompiler::CustomPrimitiveData(CMaterialExpression_CustomPrimitiveData* Node, ECustomPrimitiveDataType Type)
	{
		Node->Output->SetInputType(EMaterialInputType::Float);
		
		switch (Type)
		{
		case ECustomPrimitiveDataType::Float:
			ShaderChunks.append("float " + Node->GetNodeFullName() + " = InstanceData.CustomData.AsFloat;\n");
			break;
		case ECustomPrimitiveDataType::Int:
			ShaderChunks.append("int " + Node->GetNodeFullName() + " = InstanceData.CustomData.AsInt;\n");
			break;
		case ECustomPrimitiveDataType::UInt:
			ShaderChunks.append("uint " + Node->GetNodeFullName() + " = InstanceData.CustomData.AsUInt;\n");
			break;
		case ECustomPrimitiveDataType::Float4:
			ShaderChunks.append("float4 " + Node->GetNodeFullName() + " = InstanceData.CustomData.AsFloat4;\n");
			Node->Output->SetInputType(EMaterialInputType::Float4);
			Node->Output->SetComponentMask(EComponentMask::RGBA);
			break;
		case ECustomPrimitiveDataType::Bool:
			ShaderChunks.append("bool " + Node->GetNodeFullName() + " = InstanceData.CustomData.AsBool;\n");
			break;
		}
	}

	// ========================================================================
	// Math Operations
	// ========================================================================

	void FMaterialCompiler::Multiply(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Multiplication* Node = A->GetOwningNode<CMaterialExpression_Multiplication>();
		Node->Output->InputType = EmitBinaryOp("*", A, B, Node->ConstA, Node->ConstB, true);
	}

	void FMaterialCompiler::Divide(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Division* Node = A->GetOwningNode<CMaterialExpression_Division>();
		Node->Output->InputType = EmitBinaryOp("/", A, B, Node->ConstA, Node->ConstB, true);
	}

	void FMaterialCompiler::Add(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Addition* Node = A->GetOwningNode<CMaterialExpression_Addition>();
		Node->Output->InputType = EmitBinaryOp("+", A, B, Node->ConstA, Node->ConstB, true);
	}

	void FMaterialCompiler::Subtract(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Subtraction* Node = A->GetOwningNode<CMaterialExpression_Subtraction>();
		Node->Output->InputType = EmitBinaryOp("-", A, B, Node->ConstA, Node->ConstB, true);
	}

	void FMaterialCompiler::Sin(CMaterialInput* A, CMaterialInput* B)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Sin* Node = A->GetOwningNode<CMaterialExpression_Sin>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = sin(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::Cos(CMaterialInput* A, CMaterialInput* B)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Cosin* Node = A->GetOwningNode<CMaterialExpression_Cosin>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = cos(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::Fract(CMaterialInput* A)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Fract* Node = A->GetOwningNode<CMaterialExpression_Fract>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = frac(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::Floor(CMaterialInput* A, CMaterialInput* B)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Floor* Node = A->GetOwningNode<CMaterialExpression_Floor>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = floor(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::Ceil(CMaterialInput* A, CMaterialInput* B)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Ceil* Node = A->GetOwningNode<CMaterialExpression_Ceil>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = ceil(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::Power(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Power* Node = A->GetOwningNode<CMaterialExpression_Power>();

		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		// Check for invalid operations
		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform Power between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = pow(" + AValue.Value + ", " + BValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::Mod(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Mod* Node = A->GetOwningNode<CMaterialExpression_Mod>();

		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform Mod between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = fmod(" + AValue.Value + ", " + BValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::Min(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Min* Node = A->GetOwningNode<CMaterialExpression_Min>();

		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		// Check for invalid operations
		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform Power between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = min(" + AValue.Value + ", " + BValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::Max(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Max* Node = A->GetOwningNode<CMaterialExpression_Max>();

		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform Power between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = max(" + AValue.Value + ", " + BValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::Step(CMaterialInput* A, CMaterialInput* B)
	{
		CMaterialExpression_Step* Node = A->GetOwningNode<CMaterialExpression_Step>();

		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform Power between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = step(" + AValue.Value + ", " + BValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::Lerp(CMaterialInput* A, CMaterialInput* B, CMaterialInput* C)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Lerp* Node = A->GetOwningNode<CMaterialExpression_Lerp>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);
		FInputValue CValue = GetTypedInputValue(C, Node->Alpha);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform Power between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = lerp(" + AValue.Value + ", " + BValue.Value + ", " + CValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::Clamp(CMaterialInput* A, CMaterialInput* B, CMaterialInput* C)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_Clamp* Node = A->GetOwningNode<CMaterialExpression_Clamp>();

		FInputValue XValue = GetTypedInputValue(C, "1.0f");
		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		ResultType = DetermineResultType(ResultType, XValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		// Check for invalid operations
		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && XValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount != XValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform clamp between " + GetVectorType(XValue.Type) + ", " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = clamp(" + XValue.Value + ", " + AValue.Value + ", " + BValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	void FMaterialCompiler::SmoothStep(CMaterialInput* A, CMaterialInput* B, CMaterialInput* C)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();
		CMaterialExpression_SmoothStep* Node = A->GetOwningNode<CMaterialExpression_SmoothStep>();

		FInputValue AValue = GetTypedInputValue(A, Node->ConstA);
		FInputValue BValue = GetTypedInputValue(B, Node->ConstB);
		FInputValue CValue = GetTypedInputValue(C, Node->X);

		EMaterialInputType ResultType = DetermineResultType(AValue.Type, BValue.Type, true);
		ResultType = DetermineResultType(ResultType, CValue.Type, true);
		FString TypeStr = GetVectorType(ResultType);

		// Check for invalid operations
		if (AValue.ComponentCount > 1 && BValue.ComponentCount > 1 && AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Type Mismatch";
			Error.Description = "Cannot perform smoothstep between " + GetVectorType(AValue.Type) + " and " + GetVectorType(BValue.Type);
			AddError(Error);

			ShaderChunks.append("// ERROR: Type mismatch\n");
		}

		ShaderChunks.append(TypeStr + " " + OwningNode + " = smoothstep(" + AValue.Value + ", " + BValue.Value + ", " + CValue.Value + ");\n");
		Node->Output->SetInputType(ResultType);
	}

	// ========================================================================
	// Vector Operations
	// ========================================================================

	void FMaterialCompiler::Saturate(CMaterialInput* A, CMaterialInput* /*B*/)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue AValue = GetTypedInputValue(A, "0.0");
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = clamp(" + AValue.Value + ", " + TypeStr + "(0.0), " + TypeStr + "(1.0));\n");
	}

	void FMaterialCompiler::Normalize(CMaterialInput* A, CMaterialInput* /*B*/)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue AValue = GetTypedInputValue(A, "vec3(0.0, 0.0, 1.0)");

		// Normalize requires at least vec2
		if (AValue.ComponentCount < 2)
		{
			EdNodeGraph::FError Error;
			Error.Name = "Invalid Type";
			Error.Description = "Normalize requires at least a vec2 input";
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			AddError(Error);

			// Default to vec3
			AValue.Value = "vec3(0.0, 0.0, 1.0)";
			AValue.Type = EMaterialInputType::Float3;
			AValue.ComponentCount = 3;
		}

		FString TypeStr = GetVectorType(AValue.Type);
		ShaderChunks.append(TypeStr + " " + OwningNode + " = normalize(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::Distance(CMaterialInput* A, CMaterialInput* B)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue AValue = GetTypedInputValue(A, "0.0");
		FInputValue BValue = GetTypedInputValue(B, "0.0");

		// Distance requires vectors of same dimension
		if (AValue.ComponentCount < 2)
		{
			AValue.Value = "vec3(" + AValue.Value + ")";
			AValue.ComponentCount = 3;
		}

		if (BValue.ComponentCount < 2)
		{
			BValue.Value = "vec3(" + BValue.Value + ")";
			BValue.ComponentCount = 3;
		}

		if (AValue.ComponentCount != BValue.ComponentCount)
		{
			EdNodeGraph::FError Error;
			Error.Name = "Type Mismatch";
			Error.Description = "Distance requires vectors of the same dimension";
			Error.Node = A->GetOwningNode<CMaterialGraphNode>();
			AddError(Error);
		}

		ShaderChunks.append("float " + OwningNode + " = distance(" + AValue.Value + ", " + BValue.Value + ");\n");
	}

	void FMaterialCompiler::Abs(CMaterialInput* A, CMaterialInput* /*B*/)
	{
		FString OwningNode = A->GetOwningNode()->GetNodeFullName();

		FInputValue AValue = GetTypedInputValue(A, "0.0");
		FString TypeStr = GetVectorType(AValue.Type);

		ShaderChunks.append(TypeStr + " " + OwningNode + " = abs(" + AValue.Value + ");\n");
	}

	void FMaterialCompiler::TerrainLayerWeight(const FString& ID, uint32 LayerIndex, CMaterialGraphNode* Node)
	{
		if (CurrentMaterialType != EMaterialType::Terrain)
		{
			EdNodeGraph::FError Error;
			Error.Node = Node;
			Error.Name = "Invalid Material Type";
			Error.Description = "TerrainLayerWeight is only usable in Terrain materials.";
			AddError(Error);
			ShaderChunks.append("float " + ID + " = 0.0;\n");
			return;
		}

		if (LayerIndex > 3)
		{
			LayerIndex = 3;
		}

		const char* Swizzle = "x";
		switch (LayerIndex)
		{
			case 0: Swizzle = "x"; break;
			case 1: Swizzle = "y"; break;
			case 2: Swizzle = "z"; break;
			case 3: Swizzle = "w"; break;
		}
		ShaderChunks.append("float " + ID + " = GetTerrainLayerWeights4(HeightUV)." + FString(Swizzle) + ";\n");
	}

	void FMaterialCompiler::TerrainLayerWeights(const FString& ID, CMaterialGraphNode* Node)
	{
		if (CurrentMaterialType != EMaterialType::Terrain)
		{
			EdNodeGraph::FError Error;
			Error.Node = Node;
			Error.Name = "Invalid Material Type";
			Error.Description = "TerrainLayerWeights is only usable in Terrain materials.";
			AddError(Error);
			ShaderChunks.append("float4 " + ID + " = float4(1.0, 0.0, 0.0, 0.0);\n");
			return;
		}

		ShaderChunks.append("float4 " + ID + " = GetTerrainLayerWeights4(HeightUV);\n");
	}

	void FMaterialCompiler::TerrainLayerBlend(CMaterialInput* Layer0, CMaterialInput* Layer1, CMaterialInput* Layer2, CMaterialInput* Layer3)
	{
		FString OwningNode = Layer0->GetOwningNode()->GetNodeFullName();

		if (CurrentMaterialType != EMaterialType::Terrain)
		{
			EdNodeGraph::FError Error;
			Error.Node = Layer0->GetOwningNode<CMaterialGraphNode>();
			Error.Name = "Invalid Material Type";
			Error.Description = "TerrainLayerBlend is only usable in Terrain materials.";
			AddError(Error);
			ShaderChunks.append("float3 " + OwningNode + " = float3(0.0);\n");
			return;
		}

		FInputValue L0 = GetTypedInputValue(Layer0, "float3(0.0)");
		FInputValue L1 = GetTypedInputValue(Layer1, "float3(0.0)");
		FInputValue L2 = GetTypedInputValue(Layer2, "float3(0.0)");
		FInputValue L3 = GetTypedInputValue(Layer3, "float3(0.0)");

		auto Coerce = [](const FInputValue& V) -> FString
		{
			if (V.ComponentCount >= 4)
			{
				return "float3(" + V.Value + GetSwizzleForMask(V.Mask) + ".xyz)";
			}
			if (V.ComponentCount == 3)
			{
				return V.Value + GetSwizzleForMask(V.Mask);
			}
			if (V.ComponentCount == 2)
			{
				return "float3(" + V.Value + GetSwizzleForMask(V.Mask) + ", 0.0)";
			}
			return "float3(" + V.Value + GetSwizzleForMask(V.Mask) + ")";
		};

		FString L0Str = Coerce(L0);
		FString L1Str = Coerce(L1);
		FString L2Str = Coerce(L2);
		FString L3Str = Coerce(L3);

		ShaderChunks.append("float4 " + OwningNode + "_W = GetTerrainLayerWeights4(HeightUV);\n");
		ShaderChunks.append("float3 " + OwningNode + " = "
			+ L0Str + " * " + OwningNode + "_W.x + "
			+ L1Str + " * " + OwningNode + "_W.y + "
			+ L2Str + " * " + OwningNode + "_W.z + "
			+ L3Str + " * " + OwningNode + "_W.w;\n");
	}

	void FMaterialCompiler::GetBoundTextures(TVector<TObjectPtr<CTexture>>& Images)
	{
		Images = BoundImages;
	}

	void FMaterialCompiler::AddRaw(const FString& Raw)
	{
		ShaderChunks.append(Raw);
	}
}