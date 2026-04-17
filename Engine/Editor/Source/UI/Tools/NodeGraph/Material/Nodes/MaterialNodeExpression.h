#pragma once
#include "MaterialGraphNode.h"
#include "UI/Tools/NodeGraph/Material/MaterialInput.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"
#include "MaterialNodeExpression.generated.h"

namespace Lumina
{
    REFLECT()
    class CMaterialExpression : public CMaterialGraphNode
    {
        GENERATED_BODY()
        
    public:
        
        void BuildNode() override;
        
        CMaterialOutput* Output;

        PROPERTY(Editable, Category = "Dynamic")
        bool bDynamic = false;
        
    };

    REFLECT()
    class CMaterialExpression_Math : public CMaterialExpression
    {
        GENERATED_BODY()
        
    public:
        void BuildNode() override;
        
        FFixedString GetNodeCategory() const override { return "Math"; }
        
        CMaterialInput* A = nullptr;
        CMaterialInput* B = nullptr;

        PROPERTY(Editable, Category = "Value")
        float ConstA = 0;

        PROPERTY(Editable, Category = "Value")
        float ConstB = 0;
    };

    REFLECT()
    class CMaterialExpression_Addition : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Add"; }
        FString GetNodeTooltip() const override { return "Returns A + B, per component."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Clamp : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Clamp"; }
        FString GetNodeTooltip() const override { return "Clamps X to the inclusive range [A, B]."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        CMaterialInput* X = nullptr;

    };

    REFLECT()
    class CMaterialExpression_Saturate : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Saturate"; }
        FString GetNodeTooltip() const override { return "Clamps the input to the [0, 1] range. Equivalent to clamp(x, 0, 1)."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Normalize : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Normalize"; }
        FString GetNodeTooltip() const override { return "Returns a vector with the same direction as the input and length 1."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Distance : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Distance"; }
        FString GetNodeTooltip() const override { return "Returns the scalar distance between points A and B."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Abs : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Abs"; }
        FString GetNodeTooltip() const override { return "Returns the absolute value of A, per component."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_SmoothStep : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "SmoothStep"; }
        FString GetNodeTooltip() const override { return "Hermite-interpolates between 0 and 1 as X moves from A to B. Returns 0 below A, 1 above B."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        CMaterialInput* C = nullptr;


        PROPERTY(Editable, Category = "Value")
        float X = 0.5f;

    };

    REFLECT()
    class CMaterialExpression_Subtraction : public CMaterialExpression_Math
    {
        GENERATED_BODY()

    public:

        void BuildNode() override;


        FString GetNodeDisplayName() const override { return "Subtract"; }
        FString GetNodeTooltip() const override { return "Returns A - B, per component."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Multiplication : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Multiply"; }
        FString GetNodeTooltip() const override { return "Returns A * B, per component."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };


    REFLECT()
    class CMaterialExpression_Sin : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Sin"; }
        FString GetNodeTooltip() const override { return "Returns the sine of A. A is expected to be in radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Cosin : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Cosin"; }
        FString GetNodeTooltip() const override { return "Returns the cosine of A. A is expected to be in radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Floor : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Floor"; }
        FString GetNodeTooltip() const override { return "Returns the largest integer <= A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Fract : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Fract"; }
        FString GetNodeTooltip() const override { return "Returns the fractional part of A (A - floor(A)). Useful for tiling/wrapping."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Ceil : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Ceil"; }
        FString GetNodeTooltip() const override { return "Returns the smallest integer >= A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Power : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Power"; }
        FString GetNodeTooltip() const override { return "Returns A raised to the B-th power (pow(A, B))."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Mod : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Mod"; }
        FString GetNodeTooltip() const override { return "Returns the floating-point remainder of A / B."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Min : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Min"; }
        FString GetNodeTooltip() const override { return "Returns the component-wise minimum of A and B."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Max : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Max"; }
        FString GetNodeTooltip() const override { return "Returns the component-wise maximum of A and B."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Step : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:


        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Step"; }
        FString GetNodeTooltip() const override { return "Returns 0 if A < B, 1 otherwise. Hard threshold."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };

    REFLECT()
    class CMaterialExpression_Lerp : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Lerp"; }
        FString GetNodeTooltip() const override { return "Linearly interpolates between A and B by alpha C. Returns A when C=0, B when C=1."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        PROPERTY(Editable, Category = "Value")
        float Alpha = 0;


        CMaterialInput* C = nullptr;
    };


    REFLECT()
    class CMaterialExpression_Division : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FString GetNodeDisplayName() const override { return "Divide"; }
        FString GetNodeTooltip() const override { return "Returns A / B, per component. Beware division by zero."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

    };


    //============================================================================================

    REFLECT()
    class CMaterialExpression_ComponentMask : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override;
        FString GetNodeTooltip() const override { return "Selects a subset of the input vector's channels (R/G/B/A) to pass through. Output width equals the number of enabled channels."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
        ImVec2 GetMinNodeTitleBarSize() const override;

        CMaterialInput* InputPin = nullptr;

        PROPERTY(Editable)
        bool R = true;

        PROPERTY(Editable)
        bool G = true;

        PROPERTY(Editable)
        bool B = true;

        PROPERTY(Editable)
        bool A = true;

    };

    REFLECT()
    class CMaterialExpression_Append : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "Append"; }
        FString GetNodeTooltip() const override { return "Concatenates the channels of A and B into a wider vector."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialInput* InputA = nullptr;
        CMaterialInput* InputB = nullptr;
    };

    REFLECT()
    class CMaterialExpression_MakeFloat2 : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "MakeFloat2"; }
        FString GetNodeTooltip() const override { return "Combines two scalars into a float2 (R, G)."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialInput* R = nullptr;
        CMaterialInput* G = nullptr;
    };

    REFLECT()
    class CMaterialExpression_MakeFloat3 : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "MakeFloat3"; }
        FString GetNodeTooltip() const override { return "Combines three scalars into a float3 (R, G, B)."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialInput* R = nullptr;
        CMaterialInput* G = nullptr;
        CMaterialInput* B = nullptr;
    };

    REFLECT()
    class CMaterialExpression_MakeFloat4 : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "MakeFloat4"; }
        FString GetNodeTooltip() const override { return "Combines four scalars into a float4 (R, G, B, A)."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialInput* R = nullptr;
        CMaterialInput* G = nullptr;
        CMaterialInput* B = nullptr;
        CMaterialInput* A = nullptr;
    };

    REFLECT()
    class CMaterialExpression_BreakFloat2 : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "BreakFloat2"; }
        FString GetNodeTooltip() const override { return "Splits a float2 into its R and G scalar components."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialInput* InputPin = nullptr;
        CMaterialOutput* R = nullptr;
        CMaterialOutput* G = nullptr;
    };

    REFLECT()
    class CMaterialExpression_BreakFloat3 : public CMaterialExpression_BreakFloat2
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "BreakFloat3"; }
        FString GetNodeTooltip() const override { return "Splits a float3 into its R, G and B scalar components."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialOutput* B = nullptr;
    };

    REFLECT()
    class CMaterialExpression_BreakFloat4 : public CMaterialExpression_BreakFloat3
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "BreakFloat4"; }
        FString GetNodeTooltip() const override { return "Splits a float4 into its R, G, B and A scalar components."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialOutput* A = nullptr;
    };

    REFLECT()
    class CMaterialExpression_WorldPos : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        uint32 GetNodeTitleColor() const override { return IM_COL32(25, 25, 255, 255); }
        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "WorldPosition"; }
        FString GetNodeTooltip() const override { return "Returns the current fragment's world-space position (float3)."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_CameraPos : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        uint32 GetNodeTitleColor() const override { return IM_COL32(25, 25, 255, 255); }
        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "CameraPosition"; }
        FString GetNodeTooltip() const override { return "Returns the world-space position of the active camera (float3)."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_EntityID : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        uint32 GetNodeTitleColor() const override { return IM_COL32(25, 25, 255, 255); }
        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "EntityID"; }
        FString GetNodeTooltip() const override { return "Returns the ID of the entity being rendered. Useful for per-entity effects."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_VertexNormal : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        uint32 GetNodeTitleColor() const override { return IM_COL32(25, 25, 255, 255); }
        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "VertexNormal"; }
        FString GetNodeTooltip() const override { return "Returns the interpolated world-space vertex normal (float3)."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Panner : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        void BuildNode() override;

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "Panner"; }
        FString GetNodeTooltip() const override { return "Offsets UV coordinates over time. UV += Time * Speed. Useful for scrolling textures."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;


        CMaterialInput* UV = nullptr;
        CMaterialInput* Time = nullptr;
        CMaterialInput* Speed = nullptr;

        PROPERTY(Editable)
        float SpeedX = 1.0f;

        PROPERTY(Editable)
        float SpeedY = 1.0f;
    };

    REFLECT()
    class CMaterialExpression_TexCoords : public CMaterialExpression
    {
        GENERATED_BODY()
    public:

        FFixedString GetNodeCategory() const override { return "Utility"; }
        FString GetNodeDisplayName() const override { return "TexCoords"; }
        FString GetNodeTooltip() const override { return "Returns the mesh's UV coordinates from the given texcoord set, scaled by the tiling factors."; }
        void* GetNodeDefaultValue() override { return nullptr; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        PROPERTY(Editable)
        uint32 TextureIndex = 0;

        PROPERTY(Editable)
        float UTiling = 1.0f;

        PROPERTY(Editable)
        float VTiling = 1.0f;
    };

    //============================================================================================


    REFLECT()
    class CMaterialExpression_Constant : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        
        
        FFixedString GetNodeCategory() const override { return "Constants"; }
        void DrawContextMenu() override;
        void DrawNodeTitleBar() override;
        void BuildNode() override;
        
        void* GetNodeDefaultValue() override { return &Value.r; }

        PROPERTY(Editable, Category = "Parameter")
        FName               ParameterName;

        PROPERTY(Editable, Color, Category = "Value")
        glm::vec4           Value = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        
        EMaterialInputType  ValueType = EMaterialInputType::Float;
    };

    REFLECT()
    class CMaterialExpression_ConstantFloat : public CMaterialExpression_Constant
    {
        GENERATED_BODY()
    public:
        
        CMaterialExpression_ConstantFloat()
        {
            ValueType = EMaterialInputType::Float;
        }

        FString GetNodeDisplayName() const override { return "Float"; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        void DrawNodeBody() override;
        
    };

    REFLECT()
    class CMaterialExpression_ConstantFloat2 : public CMaterialExpression_Constant
    {
        GENERATED_BODY()
    public:

        CMaterialExpression_ConstantFloat2()
        {
            ValueType = EMaterialInputType::Float2;
        }
        
        FString GetNodeDisplayName() const override { return "Float2"; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        void DrawNodeBody() override;
    };

    REFLECT()
    class CMaterialExpression_ConstantFloat3 : public CMaterialExpression_Constant
    {
        GENERATED_BODY()
    public:
        
        
        CMaterialExpression_ConstantFloat3()
        {
            ValueType = EMaterialInputType::Float3;
        }

        FString GetNodeDisplayName() const override { return "Float3"; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        void DrawNodeBody() override;
        
    };

    REFLECT()
    class CMaterialExpression_ConstantFloat4 : public CMaterialExpression_Constant
    {
        GENERATED_BODY()
    public:
        
        CMaterialExpression_ConstantFloat4()
        {
            ValueType = EMaterialInputType::Float4;
        }

        FString GetNodeDisplayName() const override { return "Float4"; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        void DrawNodeBody() override;

    };
}
