#pragma once
#include "MaterialInput.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Renderer/CustomPrimitiveData.h"

namespace Lumina
{
    class CMaterialExpression_CustomPrimitiveData;
    class CTexture;
    class FMaterialNodePin;
    class CMaterialGraphNode;
    class CMaterialInput;
    class CMaterialOutput;
}


namespace Lumina
{
    class FMaterialCompiler
    {
    public:
        
        struct FScalarParam
        {
            uint16 Index;
            float Value;
        };

        struct FVectorParam
        {
            uint16 Index;
            glm::vec4 Value;
        };

        struct FNodeOutputInfo
        {
            EMaterialInputType Type;
            EComponentMask Mask;
            FString NodeName;
        };

        struct FInputValue
        {
            FString             Value;
            EMaterialInputType  Type;
            EComponentMask      Mask;
            int32               ComponentCount;
        };

    public:
        FMaterialCompiler();

        FString BuildTree(size_t& StartReplacement, size_t& EndReplacement, EMaterialType MaterialType = EMaterialType::PBR) const;
        
        // Parameter definitions
        void DefineFloatParameter(const FString& NodeID, const FName& ParamID, float Value);
        void DefineFloat2Parameter(const FString& NodeID, const FName& ParamID, float Value[2]);
        void DefineFloat3Parameter(const FString& NodeID, const FName& ParamID, float Value[3]);
        void DefineFloat4Parameter(const FString& NodeID, const FName& ParamID, float Value[4]);

        // Constant definitions
        void DefineConstantFloat(const FString& ID, float Value);
        void DefineConstantFloat2(const FString& ID, float Value[2]);
        void DefineConstantFloat3(const FString& ID, float Value[3]);
        void DefineConstantFloat4(const FString& ID, float Value[4]);
        
        // Data Type operations.
        void BreakFloat2(CMaterialInput* A);
        void BreakFloat3(CMaterialInput* A);
        void BreakFloat4(CMaterialInput* A);
        
        void MakeFloat2(CMaterialInput* R, CMaterialInput* G);
        void MakeFloat3(CMaterialInput* R, CMaterialInput* G, CMaterialInput* B);
        void MakeFloat4(CMaterialInput* R, CMaterialInput* G, CMaterialInput* B, CMaterialInput* A);
        
        void ComponentMask(CMaterialInput* A);
        
        // Texture operations
        void DefineTextureSample(const FString& ID);
        void TextureSample(const FString& ID, CTexture* Texture, CMaterialInput* Input);

        // Built-in inputs
        void VertexNormal(const FString& ID);
        void TexCoords(const FString& ID, uint32 Index, float UTiling, float VTiling);
        void Panner(CMaterialInput* UV, CMaterialInput* Time, CMaterialInput* Speed);
        void WorldPos(const FString& ID);
        void CameraPos(const FString& ID);
        void EntityID(const FString& ID);
        void Time(const FString& ID);
        void CustomPrimitiveData(CMaterialExpression_CustomPrimitiveData* Node, ECustomPrimitiveDataType Type);

        // Math operations
        void Multiply(CMaterialInput* A, CMaterialInput* B);
        void Divide(CMaterialInput* A, CMaterialInput* B);
        void Add(CMaterialInput* A, CMaterialInput* B);
        void Subtract(CMaterialInput* A, CMaterialInput* B);
        void Sin(CMaterialInput* A, CMaterialInput* B);
        void Cos(CMaterialInput* A, CMaterialInput* B);
        void Fract(CMaterialInput* A);
        void Floor(CMaterialInput* A, CMaterialInput* B);
        void Ceil(CMaterialInput* A, CMaterialInput* B);
        void Power(CMaterialInput* A, CMaterialInput* B);
        void Mod(CMaterialInput* A, CMaterialInput* B);
        void Min(CMaterialInput* A, CMaterialInput* B);
        void Max(CMaterialInput* A, CMaterialInput* B);
        void Step(CMaterialInput* A, CMaterialInput* B);
        void Lerp(CMaterialInput* A, CMaterialInput* B, CMaterialInput* C);
        void Clamp(CMaterialInput* A, CMaterialInput* B, CMaterialInput* C);
        void SmoothStep(CMaterialInput* A, CMaterialInput* B, CMaterialInput* C);

        // Vector operations
        void Saturate(CMaterialInput* A, CMaterialInput* B);
        void Normalize(CMaterialInput* A, CMaterialInput* B);
        void Distance(CMaterialInput* A, CMaterialInput* B);
        void Abs(CMaterialInput* A, CMaterialInput* B);

        // Terrain-only helpers (emit an error on non-terrain materials so the graph reports it).
        void TerrainLayerWeight(const FString& ID, uint32 LayerIndex, CMaterialGraphNode* Node);
        void TerrainLayerWeights(const FString& ID, CMaterialGraphNode* Node);
        void TerrainLayerBlend(CMaterialInput* Layer0, CMaterialInput* Layer1, CMaterialInput* Layer2, CMaterialInput* Layer3);

        void SetMaterialType(EMaterialType InType) { CurrentMaterialType = InType; }
        EMaterialType GetMaterialType() const { return CurrentMaterialType; }

        void NewLine();
        void AddRaw(const FString& Raw);

        void GetBoundTextures(TVector<TObjectPtr<CTexture>>& Images);
        
        FORCEINLINE bool HasErrors() const { return !Errors.empty(); }
        FORCEINLINE void AddError(const EdNodeGraph::FError& Error) { Errors.push_back(Error); }
        FORCEINLINE const TVector<EdNodeGraph::FError>& GetErrors() const { return Errors; }

        
        FInputValue GetTypedInputValue(CMaterialInput* Input, float DefaultValue = 0.0f);
        FInputValue GetTypedInputValue(CMaterialInput* Input, const FString& DefaultValueStr);

        static int32 GetComponentCount(EComponentMask Mask);
        static int32 GetComponentCount(EMaterialInputType Type);

    private:
        
        
        EMaterialInputType DetermineResultType(EMaterialInputType A, EMaterialInputType B, bool IsComponentWise = true);
        
        NODISCARD EMaterialInputType EmitBinaryOp(const FString& Op, CMaterialInput* A, CMaterialInput* B, float DefaultA, float DefaultB, bool IsComponentWise = true);

    private:
        
        FString ShaderChunks;
        TVector<TObjectPtr<CTexture>> BoundImages;
        TVector<EdNodeGraph::FError> Errors;
        
        THashMap<FName, FScalarParam> ScalarParameters;
        THashMap<FName, FVectorParam> VectorParameters;
        
        uint16 NumScalarParams = 0;
        uint16 NumVectorParams = 0;
        uint16 NumTextureParams = 0;

        EMaterialType CurrentMaterialType = EMaterialType::PBR;
    };
}
