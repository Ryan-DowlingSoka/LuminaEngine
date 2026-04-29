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

        // Opacity (For transparent materials)
        //WorldPositionOffsetPin = CreatePin(CMaterialInput::StaticClass(), "World Position Offset (WPO)", ENodePinDirection::Input);
        //WorldPositionOffsetPin->SetPinName("World Position Offset (WPO)");
    }

    void CMaterialOutputNode::GenerateDefinition(FMaterialCompiler& Compiler)
    {
        FString Output;
        Output += "\n\n";
    
        Output += "\tFMaterialPixelInputs Material;\n";
    
        auto EmitMaterialInput = [&](const FString& InputName, CEdNodeGraphPin* Pin, const FString& DefaultValue, int32 RequiredComponents)
        {
            Output += "\tMaterial." + InputName + " = ";
            
            if (Pin->HasConnection())
            {
                CMaterialOutput* ConnectedPin = Pin->GetConnection<CMaterialOutput>(0);
                FString NodeName = ConnectedPin->GetOwningNode()->GetNodeFullName();
                
                int32 ConnectedComponents = FMaterialCompiler::GetComponentCount(ConnectedPin->GetComponentMask());
                
                FString Swizzle = GetSwizzleForMask(ConnectedPin->GetComponentMask());
                
                if (!Swizzle.empty())
                {
                    ConnectedComponents = Swizzle.length() - 1;
                }
                
                FString Value = NodeName + Swizzle;
                
                if (RequiredComponents == 1)
                {
                    if (ConnectedComponents == 1)
                    {
                        Output += Value + ";\n";
                    }
                    else
                    {
                        if (Swizzle.empty())
                        {
                            Output += Value + ".r;\n";
                        }
                        else
                        {
                            Output += Value + ".r;\n";
                        }
                    }
                }
                else if (RequiredComponents == 3)
                {
                    if (ConnectedComponents == 3)
                    {
                        Output += Value + ";\n";
                    }
                    else if (ConnectedComponents == 4)
                    {
                        if (Swizzle.empty())
                        {
                            Output += Value + ".rgb;\n";
                        }
                        else
                        {
                            Output += Value + ";\n";
                        }
                    }
                    else if (ConnectedComponents == 2)
                    {
                        Output += "float3(" + Value + ", 0.0);\n";
                    }
                    else
                    {
                        Output += "float3(" + Value + ");\n";
                    }
                }
                else
                {
                    Output += Value + ";\n";
                }
            }
            else
            {
                Output += DefaultValue + ";\n";
            }
        };
    
        EmitMaterialInput("Diffuse", BaseColorPin, "float3(1.0, 1.0, 1.0)", 3);
        EmitMaterialInput("Metallic", MetallicPin, "0.0", 1);
        EmitMaterialInput("Roughness", RoughnessPin, "1.0", 1);
        EmitMaterialInput("Specular", SpecularPin, "0.5", 1);
        EmitMaterialInput("Emissive", EmissivePin, "float3(0.0, 0.0, 0.0)", 3);
        EmitMaterialInput("AmbientOcclusion", AOPin, "1.0", 1);
        EmitMaterialInput("Normal", NormalPin, "float3(0.0, 0.0, 1.0)", 3);
        EmitMaterialInput("Opacity", OpacityPin, "1.0", 1);

        // Decode the connected Normal as a tangent-space normal map. Two
        // decode paths depending on whether the upstream sample came from
        // a NormalMap-flagged texture:
        //
        //  * NormalMap source (BC5_UNORM, 2-channel store): take only XY
        //    from the sample, decode to [-1, 1], reconstruct Z from the
        //    unit-length constraint. Works for any 2-channel tangent normal
        //    regardless of where the missing Z came from.
        //  * Non-NormalMap source (3-channel store, or hand-built float3):
        //    full xyz decode + normalize, the textbook path.
        //
        // The unconnected default `float3(0, 0, 1)` is already in [-1, 1]
        // so we only patch the connected case.
        if (NormalPin->HasConnection())
        {
            CMaterialOutput* ConnectedPin   = NormalPin->GetConnection<CMaterialOutput>(0);
            const FString    UpstreamName   = ConnectedPin->GetOwningNode()->GetNodeFullName();
            const bool       bNormalMapSrc  = Compiler.IsNormalMapSampleNode(UpstreamName);

            if (bNormalMapSrc)
            {
                // 2-channel decode + Z reconstruct. saturate() guards against
                // floating-point overshoot from xy^2 > 1 (rare, but it gives
                // sqrt(<0) = NaN if unguarded).
                Output += "\tMaterial.Normal.xy = Material.Normal.xy * 2.0 - 1.0;\n";
                Output += "\tMaterial.Normal.z  = sqrt(saturate(1.0 - dot(Material.Normal.xy, Material.Normal.xy)));\n";
            }
            else
            {
                Output += "\tMaterial.Normal = normalize(Material.Normal * 2.0 - 1.0);\n";
            }
        }
        //EmitMaterialInput("WorldPositionOffset", WorldPositionOffsetPin, "float3(0.0)", 3);
        
        
        Compiler.AddRaw(Output);
    }
}
