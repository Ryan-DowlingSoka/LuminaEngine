#pragma once
#include "Common.slang"
#include "GlobalRHI.slang"

// Globals for the UI material domain (fullscreen pass into an offscreen brush, no scene).
// Implements the material-codegen surface (GetScreenSize/GetTime/GetMaterial*) without SceneGlobals.

// Must match FUIMaterialBrushArgs in RmlUiRenderer.cpp (scalar layout).
struct FUIMaterialArgs
{
    FMaterialUniforms* Materials;
    uint4 ScreenSize;    // .xy = brush resolution
    float Time;
    uint  MaterialIndex;
    uint2 _Pad;
};

FUIMaterialArgs* UIArgs() { return GetArgs<FUIMaterialArgs>(); }

float  GetMaterialScalar (uint MatIndex, uint Index) { return UIArgs().Materials[MatIndex].Scalars[Index];  }
float4 GetMaterialVec4   (uint MatIndex, uint Index) { return UIArgs().Materials[MatIndex].Vectors[Index];  }
uint   GetMaterialTexture(uint MatIndex, uint Index) { return UIArgs().Materials[MatIndex].Textures[Index]; }

uint2  GetScreenSize() { return UIArgs().ScreenSize.xy; }
float  GetTime()       { return UIArgs().Time; }

// UI has no camera/world; neutral values for scene-dependent nodes the graph may emit.
float3 GetCameraPosition() { return float3(0.0); }
