#include "Includes/ImGuiCommon.slang"

[shader("fragment")]
float4 FragmentMain(FImGuiInterpolants Input) : SV_Target0
{
    FImGuiArgs* Args = GetArgs<FImGuiArgs>();
    return Input.Color * SampleTexture2D(Args.TextureID, Args.SamplerIndex, Input.UV);
}
