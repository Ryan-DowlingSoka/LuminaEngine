#include "Includes/ImGuiCommon.slang"

[shader("vertex")]
FImGuiInterpolants VertexMain(uint VertexID : SV_VulkanVertexID)
{
    FImGuiArgs* Args = GetArgs<FImGuiArgs>();

    ImDrawVert V = ((ImDrawVert*)Args.VertexAddr)[VertexID];

    FImGuiInterpolants Output;
    Output.Position = float4(V.Position * Args.Scale + Args.Translate, 0.0, 1.0);
    Output.Color    = float4( (V.Color        & 0xFFu),
                              ((V.Color >>  8) & 0xFFu),
                              ((V.Color >> 16) & 0xFFu),
                              ((V.Color >> 24) & 0xFFu)) / 255.0;
    Output.UV       = V.UV;
    return Output;
}
