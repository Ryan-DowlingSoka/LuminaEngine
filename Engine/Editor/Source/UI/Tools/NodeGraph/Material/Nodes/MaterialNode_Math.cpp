#include "MaterialNode_Math.h"

#include "Core/Object/Cast.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"

namespace Lumina
{
    static void BuildBinaryMathPins(CMaterialExpression_Math* Self)
    {
        Self->A = Cast<CMaterialInput>(Self->CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        Self->A->SetPinName("X");
        Self->A->SetShouldDrawEditor(true);
        Self->A->SetIndex(0);

        Self->B = Cast<CMaterialInput>(Self->CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        Self->B->SetPinName("Y");
        Self->B->SetShouldDrawEditor(true);
        Self->B->SetIndex(1);
    }

    static void BuildUnaryMathPin(CMaterialExpression_Math* Self)
    {
        Self->A = Cast<CMaterialInput>(Self->CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        Self->A->SetPinName("X");
        Self->A->SetShouldDrawEditor(true);
        Self->A->SetIndex(0);
    }

    // Binary
    void CMaterialExpression_Addition::BuildNode()       { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Subtraction::BuildNode()    { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Multiplication::BuildNode() { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Division::BuildNode()       { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Power::BuildNode()          { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Mod::BuildNode()            { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Min::BuildNode()            { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Max::BuildNode()            { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Step::BuildNode()           { Super::BuildNode(); BuildBinaryMathPins(this); }
    void CMaterialExpression_Atan2::BuildNode()
    {
        Super::BuildNode();
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        A->SetPinName("Y"); A->SetShouldDrawEditor(true); A->SetIndex(0);
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        B->SetPinName("X"); B->SetShouldDrawEditor(true); B->SetIndex(1);
    }

    // Unary
    void CMaterialExpression_Sin::BuildNode()              { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Cosin::BuildNode()            { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Tan::BuildNode()              { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Asin::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Acos::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Atan::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Sinh::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Cosh::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Tanh::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Sqrt::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Rsqrt::BuildNode()            { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Log::BuildNode()              { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Log2::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Log10::BuildNode()            { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Exp::BuildNode()              { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Exp2::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Sign::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_OneMinus::BuildNode()         { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Reciprocal::BuildNode()       { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Round::BuildNode()            { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Truncate::BuildNode()         { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Negate::BuildNode()           { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Square::BuildNode()           { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_DegreesToRadians::BuildNode() { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_RadiansToDegrees::BuildNode() { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Floor::BuildNode()            { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Fract::BuildNode()            { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Ceil::BuildNode()             { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Abs::BuildNode()              { Super::BuildNode(); BuildUnaryMathPin(this); }
    void CMaterialExpression_Saturate::BuildNode()         { Super::BuildNode(); BuildUnaryMathPin(this); }

    // Compile - binary
    void CMaterialExpression_Addition::GenerateDefinition(FMaterialCompiler& C)        { C.Add(A, B); }
    void CMaterialExpression_Subtraction::GenerateDefinition(FMaterialCompiler& C)     { C.Subtract(A, B); }
    void CMaterialExpression_Multiplication::GenerateDefinition(FMaterialCompiler& C)  { C.Multiply(A, B); }
    void CMaterialExpression_Division::GenerateDefinition(FMaterialCompiler& C)        { C.Divide(A, B); }
    void CMaterialExpression_Power::GenerateDefinition(FMaterialCompiler& C)           { C.Power(A, B); }
    void CMaterialExpression_Mod::GenerateDefinition(FMaterialCompiler& C)             { C.Mod(A, B); }
    void CMaterialExpression_Min::GenerateDefinition(FMaterialCompiler& C)             { C.Min(A, B); }
    void CMaterialExpression_Max::GenerateDefinition(FMaterialCompiler& C)             { C.Max(A, B); }
    void CMaterialExpression_Step::GenerateDefinition(FMaterialCompiler& C)            { C.Step(A, B); }
    void CMaterialExpression_Atan2::GenerateDefinition(FMaterialCompiler& C)           { C.Atan2Op(A, B); }

    // Compile - unary
    void CMaterialExpression_Sin::GenerateDefinition(FMaterialCompiler& C)              { C.Sin(A); }
    void CMaterialExpression_Cosin::GenerateDefinition(FMaterialCompiler& C)            { C.Cos(A); }
    void CMaterialExpression_Tan::GenerateDefinition(FMaterialCompiler& C)              { C.Tan(A); }
    void CMaterialExpression_Asin::GenerateDefinition(FMaterialCompiler& C)             { C.Asin(A); }
    void CMaterialExpression_Acos::GenerateDefinition(FMaterialCompiler& C)             { C.Acos(A); }
    void CMaterialExpression_Atan::GenerateDefinition(FMaterialCompiler& C)             { C.Atan(A); }
    void CMaterialExpression_Sinh::GenerateDefinition(FMaterialCompiler& C)             { C.Sinh(A); }
    void CMaterialExpression_Cosh::GenerateDefinition(FMaterialCompiler& C)             { C.Cosh(A); }
    void CMaterialExpression_Tanh::GenerateDefinition(FMaterialCompiler& C)             { C.Tanh(A); }
    void CMaterialExpression_Sqrt::GenerateDefinition(FMaterialCompiler& C)             { C.Sqrt(A); }
    void CMaterialExpression_Rsqrt::GenerateDefinition(FMaterialCompiler& C)            { C.Rsqrt(A); }
    void CMaterialExpression_Log::GenerateDefinition(FMaterialCompiler& C)              { C.Log(A); }
    void CMaterialExpression_Log2::GenerateDefinition(FMaterialCompiler& C)             { C.Log2(A); }
    void CMaterialExpression_Log10::GenerateDefinition(FMaterialCompiler& C)            { C.Log10(A); }
    void CMaterialExpression_Exp::GenerateDefinition(FMaterialCompiler& C)              { C.Exp(A); }
    void CMaterialExpression_Exp2::GenerateDefinition(FMaterialCompiler& C)             { C.Exp2(A); }
    void CMaterialExpression_Sign::GenerateDefinition(FMaterialCompiler& C)             { C.Sign(A); }
    void CMaterialExpression_OneMinus::GenerateDefinition(FMaterialCompiler& C)         { C.OneMinus(A); }
    void CMaterialExpression_Reciprocal::GenerateDefinition(FMaterialCompiler& C)       { C.Reciprocal(A); }
    void CMaterialExpression_Round::GenerateDefinition(FMaterialCompiler& C)            { C.Round(A); }
    void CMaterialExpression_Truncate::GenerateDefinition(FMaterialCompiler& C)         { C.Truncate(A); }
    void CMaterialExpression_Negate::GenerateDefinition(FMaterialCompiler& C)           { C.Negate(A); }
    void CMaterialExpression_Square::GenerateDefinition(FMaterialCompiler& C)           { C.Square(A); }
    void CMaterialExpression_DegreesToRadians::GenerateDefinition(FMaterialCompiler& C) { C.DegreesToRadians(A); }
    void CMaterialExpression_RadiansToDegrees::GenerateDefinition(FMaterialCompiler& C) { C.RadiansToDegrees(A); }
    void CMaterialExpression_Floor::GenerateDefinition(FMaterialCompiler& C)            { C.Floor(A); }
    void CMaterialExpression_Fract::GenerateDefinition(FMaterialCompiler& C)            { C.Fract(A); }
    void CMaterialExpression_Ceil::GenerateDefinition(FMaterialCompiler& C)             { C.Ceil(A); }
    void CMaterialExpression_Abs::GenerateDefinition(FMaterialCompiler& C)              { C.Abs(A); }
    void CMaterialExpression_Saturate::GenerateDefinition(FMaterialCompiler& C)         { C.Saturate(A); }

    // Ternary
    void CMaterialExpression_Lerp::BuildNode()
    {
        Super::BuildNode();
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        A->SetPinName("X"); A->SetShouldDrawEditor(true); A->SetIndex(0);
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Y", ENodePinDirection::Input));
        B->SetPinName("Y"); B->SetShouldDrawEditor(true); B->SetIndex(1);
        C = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "A", ENodePinDirection::Input));
        C->SetPinName("A"); C->SetShouldDrawEditor(true); C->SetIndex(2);
    }
    void CMaterialExpression_Lerp::GenerateDefinition(FMaterialCompiler& Compiler) { Compiler.Lerp(A, B, C); }

    void CMaterialExpression_Clamp::BuildNode()
    {
        Super::BuildNode();
        X = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        X->SetPinName("X"); X->SetShouldDrawEditor(true); X->SetIndex(0);
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "A", ENodePinDirection::Input));
        A->SetPinName("A"); A->SetShouldDrawEditor(true); A->SetIndex(1);
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "B", ENodePinDirection::Input));
        B->SetPinName("B"); B->SetShouldDrawEditor(true); B->SetIndex(2);
    }
    void CMaterialExpression_Clamp::GenerateDefinition(FMaterialCompiler& Compiler) { Compiler.Clamp(A, B, X); }

    void CMaterialExpression_SmoothStep::BuildNode()
    {
        Super::BuildNode();
        A = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Edge0", ENodePinDirection::Input));
        A->SetPinName("Edge0"); A->SetShouldDrawEditor(true); A->SetIndex(0);
        B = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "Edge1", ENodePinDirection::Input));
        B->SetPinName("Edge1"); B->SetShouldDrawEditor(true); B->SetIndex(1);
        C = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        C->SetPinName("X"); C->SetShouldDrawEditor(true); C->SetIndex(2);
    }
    void CMaterialExpression_SmoothStep::GenerateDefinition(FMaterialCompiler& Compiler) { Compiler.SmoothStep(A, B, C); }

    void CMaterialExpression_Remap::BuildNode()
    {
        Super::BuildNode();
        X      = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "X", ENodePinDirection::Input));
        InMin  = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "InMin", ENodePinDirection::Input));
        InMax  = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "InMax", ENodePinDirection::Input));
        OutMin = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "OutMin", ENodePinDirection::Input));
        OutMax = Cast<CMaterialInput>(CreatePin(CMaterialInput::StaticClass(), "OutMax", ENodePinDirection::Input));
    }
    void CMaterialExpression_Remap::GenerateDefinition(FMaterialCompiler& Compiler) { Compiler.Remap(X, InMin, InMax, OutMin, OutMax); }
}
