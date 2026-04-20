// -----------------------------------------------------------------------------
//  ParticleSimulateTemplate.slang
//  Skeleton compiled by FParticleCompiler for graph-driven particle systems.
//  The spawn and update function tokens further down are replaced with the
//  body of the user's graph at compile time.
// -----------------------------------------------------------------------------

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

struct FParticleSimParams
{
    float4  EmitterPosition;     // xyz = world position
    float4  EmitterForward;      // xyz = forward,  w = EmitterVelocity.x
    float4  EmitterRight;        // xyz = right,    w = EmitterVelocity.y
    float4  EmitterUp;           // xyz = up,       w = EmitterVelocity.z

    uint4   Counts;              // x=MaxParticles, y=SpawnCount, z=FrameSeed, w=SimFlags
    uint4   Modes;

    float4  ShapeSize;
    float4  VelocityMin;
    float4  VelocityMax;
    float4  SpeedAndLifetime;
    float4  Gravity;

    float4  StartColor;
    float4  EndColor;

    float4  SizeRange;
    float4  RotationRange;

    float4  NoiseStrength;
    float4  NoiseParams;         // x=NoiseSpeed, y=InheritEmitterVelocity

    float4  Timing;              // x=DeltaTime, y=TotalTime
};

[[vk::binding(0, 0)]] RWStructuredBuffer<FGPUParticle>    Particles;
[[vk::binding(1, 0)]] ConstantBuffer<FParticleSimParams>  SimParams;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>            SpawnCounter;

uint PCGHash(uint state)
{
    state = state * 747796405u + 2891336453u;
    uint w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (w >> 22u) ^ w;
}

float RandUnit(inout uint state)
{
    state = PCGHash(state);
    return float(state) / 4294967295.0;
}

struct FParticleSpawnResult
{
    float3  InitialVelocity;
    float   Lifetime;
    float4  StartColor;
    float   StartSize;
};

$PARTICLE_SPAWN_FUNC

$PARTICLE_UPDATE_FUNC

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Index = DispatchThreadID.x;
    if (Index >= SimParams.Counts.x) return;

    FGPUParticle P = Particles[Index];

    bool bAlive = (P.Lifetime > 0.0) && (P.Age < P.Lifetime);

    if (!bAlive)
    {
        uint myAlloc;
        InterlockedAdd(SpawnCounter[0], 1u, myAlloc);
        if (myAlloc < SimParams.Counts.y)
        {
            FParticleSpawnResult Spawn = GetParticleSpawnInfo(SimParams.Timing.y, Index);

            float3 EmitterVelocity = float3(SimParams.EmitterForward.w,
                                            SimParams.EmitterRight.w,
                                            SimParams.EmitterUp.w);

            P.Position      = SimParams.EmitterPosition.xyz;
            P.Velocity      = Spawn.InitialVelocity + EmitterVelocity * SimParams.NoiseParams.y;
            P.Lifetime      = max(Spawn.Lifetime, 0.01);
            P.Color         = Spawn.StartColor;
            P.Size          = Spawn.StartSize;
            P.Age           = 0.0;
            P.Rotation      = 0.0;
            P.RotationSpeed = 0.0;
            uint seed = Index ^ SimParams.Counts.z;
            P.Seed          = RandUnit(seed);
        }
        else
        {
            P.Lifetime = 0.0;
            P.Age      = 0.0;
            P.Color    = float4(0, 0, 0, 0);
            P.Size     = 0.0;
        }
    }
    else
    {
        P.Age += SimParams.Timing.x;
        if (P.Age >= P.Lifetime)
        {
            P.Lifetime = 0.0;
            P.Age      = 0.0;
            P.Color    = float4(0, 0, 0, 0);
            P.Size     = 0.0;
        }
        else
        {
            SimulateParticle(P, SimParams.Timing.x, SimParams.Timing.y);
        }
    }

    Particles[Index] = P;
}
