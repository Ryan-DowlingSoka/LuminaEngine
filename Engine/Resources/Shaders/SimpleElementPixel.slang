#pragma once
#include "Includes/SceneGlobals.slang"

[shader("fragment")]
float4 FragmentMain(float4 Color: COLOR) : SV_Target0
{
    return Color;
}