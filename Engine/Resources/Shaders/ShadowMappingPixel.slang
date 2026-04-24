#pragma once
#include "Includes/SceneGlobals.slang"

// Depth-only point/spot shadow PS. Emits nothing so the rasterizer's NDC z is
// stored directly — an SV_Depth export disables early-Z and forces the PS to
// run for every covered fragment, which scales with tile pixels × 6 multiview
// layers and blows up once the quad-tree promotes close lights to 2048 tiles.

struct FPushConstants
{
    int ShadowDataIndex;
    int ViewIndex;
};

[earlydepthstencil]
[shader("fragment")]
void FragmentMain(uniform FPushConstants PC)
{
}
