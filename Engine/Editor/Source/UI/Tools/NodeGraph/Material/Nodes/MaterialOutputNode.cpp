#include "MaterialOutputNode.h"


#include "UI/Tools/NodeGraph/Material/MaterialInput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"

namespace Lumina
{
    FString CMaterialOutputNode::GetNodeDisplayName() const
    {
        return "Material Output";
    }
    
    FString CMaterialOutputNode::GetNodeTooltip() const
    {
        return "The final output to the shader";
    }


    void CMaterialOutputNode::BuildNode()
    {
        // Base Color (Albedo)
        BaseColorPin = CreatePin(CMaterialInput::StaticClass(), "Base Color (RGBA)", ENodePinDirection::Input);
        BaseColorPin->SetPinName("Base Color (RGBA)");
    
        // Metallic (Determines if the material is metal or non-metal)
        MetallicPin = CreatePin(CMaterialInput::StaticClass(), "Metallic", ENodePinDirection::Input);
        MetallicPin->SetPinName("Metallic");
        
        // Roughness (Controls how smooth or rough the surface is)
        RoughnessPin = CreatePin(CMaterialInput::StaticClass(), "Roughness", ENodePinDirection::Input);
        RoughnessPin->SetPinName("Roughness");

        // Specular (Affects intensity of reflections for non-metals)
        SpecularPin = CreatePin(CMaterialInput::StaticClass(), "Specular", ENodePinDirection::Input);
        SpecularPin->SetPinName("Specular");

        // Emissive (Self-illumination, for glowing objects)
        EmissivePin = CreatePin(CMaterialInput::StaticClass(), "Emissive", ENodePinDirection::Input);
        EmissivePin->SetPinName("Emissive (RGB)");

        // Ambient Occlusion (Shadows in crevices to add realism)
        AOPin = CreatePin(CMaterialInput::StaticClass(), "Ambient Occlusion", ENodePinDirection::Input);
        AOPin->SetPinName("Ambient Occlusion");

        // Normal Map (For surface detail)
        NormalPin = CreatePin(CMaterialInput::StaticClass(), "Normal Map (XYZ)", ENodePinDirection::Input);
        NormalPin->SetPinName("Normal Map (XYZ)");
        
        // Opacity (For transparent materials)
        OpacityPin = CreatePin(CMaterialInput::StaticClass(), "Opacity", ENodePinDirection::Input);
        OpacityPin->SetPinName("Opacity");

        // World Position Offset — vertex-stage displacement in world space.
        // Routed through the compiler's vertex chunk; if connected, the
        // material's vertex shader bakes in the graph emission and adds the
        // result to WorldPos before the View/Projection transforms.
        WorldPositionOffsetPin = CreatePin(CMaterialInput::StaticClass(), "World Position Offset (WPO)", ENodePinDirection::Input);
        WorldPositionOffsetPin->SetPinName("World Position Offset (XYZ)");
    }

    // Build a `Material.<member> = <expression>;` line that handles component
    // count widening / narrowing between the connected pin's value type and
    // the expected member width. Pulled out so the same emitter serves both
    // the pixel-stage assignments and the vertex-stage (WPO) assignment.
    static FString EmitMaterialAssignment(const FString& MemberName, CEdNodeGraphPin* Pin, const FString& DefaultValue, int32 RequiredComponents)
    {
        FString Out = "\tMaterial." + MemberName + " = ";

        if (!Pin || !Pin->HasConnection())
        {
            return Out + DefaultValue + ";\n";
        }

        CMaterialOutput* ConnectedPin = Pin->GetConnection<CMaterialOutput>(0);
        FString NodeName              = ConnectedPin->GetOwningNode()->GetNodeFullName();
        int32 ConnectedComponents     = FMaterialCompiler::GetComponentCount(ConnectedPin->GetComponentMask());
        FString Swizzle               = GetSwizzleForMask(ConnectedPin->GetComponentMask());
        if (!Swizzle.empty())
        {
            ConnectedComponents = (int32)Swizzle.length() - 1;
        }
        FString Value = NodeName + Swizzle;

        if (RequiredComponents == 1)
        {
            if (ConnectedComponents == 1) Out += Value + ";\n";
            else                          Out += Value + ".r;\n";
        }
        else if (RequiredComponents == 3)
        {
            if (ConnectedComponents == 3)      Out += Value + ";\n";
            else if (ConnectedComponents == 4) Out += (Swizzle.empty() ? Value + ".rgb;\n" : Value + ";\n");
            else if (ConnectedComponents == 2) Out += "float3(" + Value + ", 0.0);\n";
            else                               Out += "float3(" + Value + ");\n";
        }
        else
        {
            Out += Value + ";\n";
        }
        return Out;
    }

    void CMaterialOutputNode::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        // ----- Pixel stage -----
        FString PixelOut;
        PixelOut += "\n\n";
        PixelOut += "\tFMaterialPixelInputs Material;\n";

        PixelOut += EmitMaterialAssignment("Diffuse",          BaseColorPin, "float3(1.0, 1.0, 1.0)", 3);
        PixelOut += EmitMaterialAssignment("Metallic",         MetallicPin,  "0.0",                    1);
        PixelOut += EmitMaterialAssignment("Roughness",        RoughnessPin, "1.0",                    1);
        PixelOut += EmitMaterialAssignment("Specular",         SpecularPin,  "0.5",                    1);
        PixelOut += EmitMaterialAssignment("Emissive",         EmissivePin,  "float3(0.0, 0.0, 0.0)", 3);
        PixelOut += EmitMaterialAssignment("AmbientOcclusion", AOPin,        "1.0",                    1);
        PixelOut += EmitMaterialAssignment("Normal",           NormalPin,    "float3(0.0, 0.0, 1.0)", 3);
        PixelOut += EmitMaterialAssignment("Opacity",          OpacityPin,   "1.0",                    1);

        // Tangent-space normal-map decode. Two paths depending on whether the
        // upstream sample came from a NormalMap-flagged texture (BC5 2-channel
        // store -> reconstruct Z) versus a 3-channel source (textbook decode).
        // Unconnected default `float3(0, 0, 1)` is already in [-1, 1] so only
        // the connected case is patched.
        if (NormalPin->HasConnection())
        {
            CMaterialOutput* ConnectedPin   = NormalPin->GetConnection<CMaterialOutput>(0);
            const FString    UpstreamName   = ConnectedPin->GetOwningNode()->GetNodeFullName();
            const bool       bNormalMapSrc  = Compiler.IsNormalMapSampleNode(UpstreamName);

            if (bNormalMapSrc)
            {
                PixelOut += "\tMaterial.Normal.xy = Material.Normal.xy * 2.0 - 1.0;\n";
                PixelOut += "\tMaterial.Normal.z  = sqrt(saturate(1.0 - dot(Material.Normal.xy, Material.Normal.xy)));\n";
            }
            else
            {
                PixelOut += "\tMaterial.Normal = normalize(Material.Normal * 2.0 - 1.0);\n";
            }
        }

        Compiler.AddPixelOutput(PixelOut);

        // ----- Vertex stage (WPO) -----
        // The vertex template already declares `FMaterialVertexInputs Material;`
        // inline above the token, so we only emit the assignment here.
        FString VertexOut;
        VertexOut += "\n";
        VertexOut += EmitMaterialAssignment("WorldPositionOffset", WorldPositionOffsetPin, "float3(0.0, 0.0, 0.0)", 3);
        Compiler.AddVertexOutput(VertexOut);
    }
}
