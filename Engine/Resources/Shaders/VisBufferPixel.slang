#include "Includes/SceneGlobals.slang"

// VisBuffer pixel shader: write the per-primitive visibility ID to the R32_UINT VisBuffer. Depth comes
// from SV_Position via the bound depth target. Material-independent -- one shader for every meshlet draw;
// only the geometry (mesh) stage is per-material (for WorldPositionOffset).

struct FVisInput
{
    nointerpolation uint VisID : VIS_ID;
    float4 Position            : SV_Position;
};

[shader("fragment")]
uint FragmentMain(FVisInput Input) : SV_Target0
{
    return Input.VisID;
}
