#pragma once
#include "Includes/SceneGlobals.slang"

// Depth-only point/spot shadow PS. Emits nothing so rasterizer NDC z stores
// directly; SV_Depth would disable early-Z and force PS per covered fragment.

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
