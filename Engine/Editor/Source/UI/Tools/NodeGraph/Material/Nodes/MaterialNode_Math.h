#pragma once
#include "MaterialNodeExpression.h"
#include "MaterialNode_Math.generated.h"

namespace Lumina
{
    // Binary math operations (A op B)

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
    class CMaterialExpression_Atan2 : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Atan2"; }
        FString GetNodeTooltip() const override { return "Returns atan2(Y, X) — angle of the (X, Y) vector in radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    // Unary math operations

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
    class CMaterialExpression_Tan : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Tan"; }
        FString GetNodeTooltip() const override { return "Returns the tangent of A (radians)."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Asin : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Asin"; }
        FString GetNodeTooltip() const override { return "Returns the arcsine of A. Result in radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Acos : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Acos"; }
        FString GetNodeTooltip() const override { return "Returns the arccosine of A. Result in radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Atan : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Atan"; }
        FString GetNodeTooltip() const override { return "Returns the arctangent of A. Result in radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Sinh : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Sinh"; }
        FString GetNodeTooltip() const override { return "Returns the hyperbolic sine of A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Cosh : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Cosh"; }
        FString GetNodeTooltip() const override { return "Returns the hyperbolic cosine of A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Tanh : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Tanh"; }
        FString GetNodeTooltip() const override { return "Returns the hyperbolic tangent of A. Useful for soft clamping."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Sqrt : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Sqrt"; }
        FString GetNodeTooltip() const override { return "Returns the square root of A. Negative inputs yield undefined."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Rsqrt : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Rsqrt"; }
        FString GetNodeTooltip() const override { return "Returns 1/sqrt(A) — fast reciprocal square root."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Log : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Log"; }
        FString GetNodeTooltip() const override { return "Returns the natural logarithm of A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Log2 : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Log2"; }
        FString GetNodeTooltip() const override { return "Returns the base-2 logarithm of A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Log10 : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Log10"; }
        FString GetNodeTooltip() const override { return "Returns the base-10 logarithm of A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Exp : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Exp"; }
        FString GetNodeTooltip() const override { return "Returns e^A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Exp2 : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Exp2"; }
        FString GetNodeTooltip() const override { return "Returns 2^A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Sign : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Sign"; }
        FString GetNodeTooltip() const override { return "Returns -1 if A<0, 0 if A==0, 1 if A>0."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_OneMinus : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "OneMinus"; }
        FString GetNodeTooltip() const override { return "Returns 1.0 - A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Reciprocal : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Reciprocal"; }
        FString GetNodeTooltip() const override { return "Returns 1.0 / A. Guarded against zero."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Round : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Round"; }
        FString GetNodeTooltip() const override { return "Rounds A to the nearest integer."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Truncate : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Truncate"; }
        FString GetNodeTooltip() const override { return "Drops the fractional part of A toward zero."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Negate : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Negate"; }
        FString GetNodeTooltip() const override { return "Returns -A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_Square : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Square"; }
        FString GetNodeTooltip() const override { return "Returns A * A."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_DegreesToRadians : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "DegToRad"; }
        FString GetNodeTooltip() const override { return "Converts degrees to radians."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    REFLECT()
    class CMaterialExpression_RadiansToDegrees : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "RadToDeg"; }
        FString GetNodeTooltip() const override { return "Converts radians to degrees."; }
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
    class CMaterialExpression_Saturate : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "Saturate"; }
        FString GetNodeTooltip() const override { return "Clamps the input to the [0, 1] range."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;
    };

    // Ternary

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

        /** Constant alpha used for the C input when the pin is not connected (0 = A, 1 = B). */
        PROPERTY(Editable, Category = "Value")
        float Alpha = 0;

        CMaterialInput* C = nullptr;
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
    class CMaterialExpression_SmoothStep : public CMaterialExpression_Math
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FString GetNodeDisplayName() const override { return "SmoothStep"; }
        FString GetNodeTooltip() const override { return "Hermite-interpolates between 0 and 1 as X moves from A to B."; }
        void* GetNodeDefaultValue() override { return &ConstA; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        CMaterialInput* C = nullptr;

        /** Constant smoothstep position used when the C input pin is not connected. */
        PROPERTY(Editable, Category = "Value")
        float X = 0.5f;
    };

    REFLECT()
    class CMaterialExpression_Remap : public CMaterialExpression
    {
        GENERATED_BODY()
    public:
        void BuildNode() override;
        FFixedString GetNodeCategory() const override { return "Math"; }
        FString GetNodeDisplayName() const override { return "Remap"; }
        FString GetNodeTooltip() const override { return "Remaps X from [InMin, InMax] to [OutMin, OutMax]."; }
        void GenerateDefinition(FMaterialCompiler& Compiler) override;

        CMaterialInput* X = nullptr;
        CMaterialInput* InMin = nullptr;
        CMaterialInput* InMax = nullptr;
        CMaterialInput* OutMin = nullptr;
        CMaterialInput* OutMax = nullptr;
    };
}
