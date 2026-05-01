#include "MaterialOutputNode.h"


#include "UI/Tools/NodeGraph/Material/MaterialInput.h"
#include "UI/Tools/NodeGraph/Material/MaterialCompiler.h"
#include "UI/Tools/NodeGraph/Material/MaterialNodeGraph.h"
#include "UI/Tools/NodeGraph/Material/MaterialOutput.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Core/Object/Cast.h"

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


    void CMaterialOutputNode::DrawNodeTitleBar()
    {
        // Refresh per-pin disabled state from the material domain. Cheap
        // (a handful of flag writes) and runs once per node draw, so the
        // greyed-out state tracks MaterialType edits without a separate
        // refresh hook.
        EMaterialType MaterialType = EMaterialType::PBR;
        if (CMaterialNodeGraph* Graph = Cast<CMaterialNodeGraph>(GetOwningGraph()))
        {
            if (CMaterial* OwningMaterial = Graph->GetMaterial())
            {
                MaterialType = OwningMaterial->GetMaterialType();
            }
        }

        const bool bPostProcess = MaterialType == EMaterialType::PostProcess;

        // PostProcess materials only consume Emissive; the surface attributes
        // are inert for the fullscreen pass. WorldPositionOffset is a vertex-
        // stage write and is meaningless without surface geometry.
        if (BaseColorPin)            BaseColorPin->SetDisabled(bPostProcess);
        if (MetallicPin)             MetallicPin->SetDisabled(bPostProcess);
        if (RoughnessPin)            RoughnessPin->SetDisabled(bPostProcess);
        if (SpecularPin)             SpecularPin->SetDisabled(bPostProcess);
        if (AOPin)                   AOPin->SetDisabled(bPostProcess);
        if (NormalPin)               NormalPin->SetDisabled(bPostProcess);
        if (OpacityPin)              OpacityPin->SetDisabled(bPostProcess);
        if (WorldPositionOffsetPin)  WorldPositionOffsetPin->SetDisabled(bPostProcess);

        // Emissive is the post-process output; always enabled.
        if (EmissivePin)             EmissivePin->SetDisabled(false);

        Super::DrawNodeTitleBar();
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
        // Mask is optional metadata -- some emitters only stamp InputType. If the mask is None
        // (count == 0) fall back to the type's intrinsic width so we don't drop into the generic
        // float3(value) branch and emit a constructor with a wider-than-expected argument.
        if (ConnectedComponents == 0)
        {
            ConnectedComponents = FMaterialCompiler::GetComponentCount(ConnectedPin->InputType);
        }
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

        // Post-process materials use Emissive as the final scene color. An
        // unconnected Emissive means "passthrough" (return the scene as-is)
        // rather than "black"; surface materials keep the legacy zero default.
        const bool bPostProcess = Compiler.GetMaterialType() == EMaterialType::PostProcess;
        const FString EmissiveDefault = bPostProcess ? FString("SceneColor.rgb") : FString("float3(0.0, 0.0, 0.0)");

        PixelOut += EmitMaterialAssignment("Diffuse",          BaseColorPin, "float3(1.0, 1.0, 1.0)", 3);
        PixelOut += EmitMaterialAssignment("Metallic",         MetallicPin,  "0.0",                    1);
        PixelOut += EmitMaterialAssignment("Roughness",        RoughnessPin, "1.0",                    1);
        PixelOut += EmitMaterialAssignment("Specular",         SpecularPin,  "0.5",                    1);
        PixelOut += EmitMaterialAssignment("Emissive",         EmissivePin,  EmissiveDefault,          3);
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
