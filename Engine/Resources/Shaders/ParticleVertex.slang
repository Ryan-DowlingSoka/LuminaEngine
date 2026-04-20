#include "Includes/SceneGlobals.slang"

// Must match FGPUParticle in ParticleSimulate.slang (64 bytes).
struct FGPUParticle
{
    float3  Position;
    float   Age;

    float3  Velocity;
    float   Lifetime;

    float4  Color;

    float   Size;
    float   Rotation;
    float   RotationSpeed;
    float   Seed;
};

// Per-emitter render params fed from the CPU. Keeps the vertex path data-driven.
struct FParticleRenderParams
{
    uint4   Flags;      // x=TextureIndex, y=BillboardToCamera, zw=reserved
    float4  Tint;       // xyz=tint rgb, w=intensity multiplier
    float4  UVParams;   // xy=scroll rate (not yet used), zw=reserved
};

[[vk::binding(0, 2)]] StructuredBuffer<FGPUParticle>        Particles;
[[vk::binding(1, 2)]] ConstantBuffer<FParticleRenderParams> RenderParams;

static const float2 QuadPositions[6] =
{
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2( 1.0,  1.0),

    float2(-1.0, -1.0),
    float2( 1.0,  1.0),
    float2(-1.0,  1.0)
};

static const float2 QuadUVs[6] =
{
    float2(0.0, 0.0),
    float2(1.0, 0.0),
    float2(1.0, 1.0),

    float2(0.0, 0.0),
    float2(1.0, 1.0),
    float2(0.0, 1.0)
};

struct VertexOutput
{
    float4                  Color        : PARTICLE_COLOR;
    float2                  UV           : TEXCOORD0;
    nointerpolation uint    TextureIndex : TEXTURE_INDEX;
    float4                  Position     : SV_Position;
};

[shader("vertex")]
VertexOutput VertexMain(uint VertexIndex : SV_VulkanVertexID)
{
    uint ParticleIndex = VertexIndex / 6;
    uint QuadIndex     = VertexIndex % 6;

    FGPUParticle P = Particles[ParticleIndex];

    VertexOutput Out;

    // Dead particles collapse to a degenerate clip-culled point.
    bool bAlive = (P.Lifetime > 0.0) && (P.Age < P.Lifetime) && (P.Size > 0.0);
    if (!bAlive)
    {
        Out.Position     = float4(2.0, 2.0, 2.0, 1.0);
        Out.Color        = float4(0.0, 0.0, 0.0, 0.0);
        Out.UV           = float2(0.0, 0.0);
        Out.TextureIndex = 0;
        return Out;
    }

    // Corner offset in the billboard's local 2D frame, rotated by particle rotation.
    float2 Local = QuadPositions[QuadIndex] * P.Size;
    float  s     = sin(P.Rotation);
    float  c     = cos(P.Rotation);
    float2 Rotated = float2(Local.x * c - Local.y * s,
                            Local.x * s + Local.y * c);

    // Billboard axes: either camera-facing or world-aligned (XZ plane).
    float3 BillboardRight;
    float3 BillboardUp;
    if (RenderParams.Flags.y != 0u)
    {
        float4x4 ViewMatrix = GetCameraView();
        BillboardRight =  float3(ViewMatrix[0][0], ViewMatrix[0][1], ViewMatrix[0][2]);
        BillboardUp    = -float3(ViewMatrix[1][0], ViewMatrix[1][1], ViewMatrix[1][2]);
    }
    else
    {
        BillboardRight = float3(1.0, 0.0, 0.0);
        BillboardUp    = float3(0.0, 0.0, 1.0);
    }

    float3 WorldPos = P.Position
                    + BillboardRight * Rotated.x
                    + BillboardUp    * Rotated.y;

    Out.UV           = QuadUVs[QuadIndex];
    Out.Color        = P.Color * float4(RenderParams.Tint.rgb * RenderParams.Tint.w, 1.0);
    Out.TextureIndex = RenderParams.Flags.x;
    Out.Position     = mul(GetCameraProjection(), mul(GetCameraView(), float4(WorldPos, 1.0)));
    return Out;
}
