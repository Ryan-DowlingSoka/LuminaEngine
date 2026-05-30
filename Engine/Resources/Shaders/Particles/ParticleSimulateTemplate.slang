// Skeleton compiled by FParticleCompiler for module-stack particle systems.

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

// Per-frame uniform block. Layout matches FParticleSimParamsGPU on the CPU and the
// default ParticleSimulate.slang, so the same CBV upload feeds both shaders.
struct FParticleSimParams
{
    float4  EmitterPosition;     // xyz = world position
    float4  EmitterForward;      // xyz = forward, w = EmitterVelocity.x
    float4  EmitterRight;        // xyz = right,   w = EmitterVelocity.y
    float4  EmitterUp;           // xyz = up,      w = EmitterVelocity.z

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

    float4  Timing;              // x=DeltaTime, y=TotalTime, z=SystemAge
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

float RandUnit(inout uint state)   { state = PCGHash(state); return float(state) / 4294967295.0; }
float RandSigned(inout uint state) { return RandUnit(state) * 2.0 - 1.0; }

float3 RandInUnitSphere(inout uint state)
{
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float3 p = float3(RandSigned(state), RandSigned(state), RandSigned(state));
        if (dot(p, p) <= 1.0) return p;
    }
    float u = RandUnit(state);
    float v = RandUnit(state);
    float theta = u * 6.2831853;
    float phi   = acos(2.0 * v - 1.0);
    float sinPhi = sin(phi);
    return float3(sinPhi * cos(theta), sinPhi * sin(theta), cos(phi));
}

float3 RandOnUnitSphere(inout uint state)
{
    float u = RandUnit(state);
    float v = RandUnit(state);
    float theta = u * 6.2831853;
    float phi   = acos(2.0 * v - 1.0);
    float sinPhi = sin(phi);
    return float3(sinPhi * cos(theta), sinPhi * sin(theta), cos(phi));
}

// Cheap value / curl noise for turbulence. Output in [-1, 1].
float Hash31(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float ValueNoise(float3 x)
{
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = Hash31(p + float3(0, 0, 0));
    float n100 = Hash31(p + float3(1, 0, 0));
    float n010 = Hash31(p + float3(0, 1, 0));
    float n110 = Hash31(p + float3(1, 1, 0));
    float n001 = Hash31(p + float3(0, 0, 1));
    float n101 = Hash31(p + float3(1, 0, 1));
    float n011 = Hash31(p + float3(0, 1, 1));
    float n111 = Hash31(p + float3(1, 1, 1));

    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);

    float nxy0 = lerp(nx00, nx10, f.y);
    float nxy1 = lerp(nx01, nx11, f.y);

    return lerp(nxy0, nxy1, f.z) * 2.0 - 1.0;
}

float3 CurlishNoise(float3 p, float time, float scale, float speed)
{
    float3 q = p * scale + float3(0.0, time * speed, 0.0);
    const float e = 0.15;
    float3 d;
    d.x = ValueNoise(q + float3(31.3, 0.0, 0.0)) - ValueNoise(q - float3(31.3, 0.0, 0.0));
    d.y = ValueNoise(q + float3(0.0, 47.7, 0.0)) - ValueNoise(q - float3(0.0, 47.7, 0.0));
    d.z = ValueNoise(q + float3(0.0, 0.0, 19.1)) - ValueNoise(q - float3(0.0, 0.0, 19.1));
    return d * (1.0 / (2.0 * e));
}

// Emitter shape sampling. Shape ids match EParticleEmitterShape.
float3 SampleEmitterShape(inout uint state, uint shape, float3 shapeSize, float coneAngle,
                          float3 forward, float3 right, float3 up)
{
    if (shape == 0u) // Point
    {
        return float3(0, 0, 0);
    }
    else if (shape == 1u) // Sphere
    {
        return RandInUnitSphere(state) * shapeSize.x;
    }
    else if (shape == 2u) // Box
    {
        return float3(RandSigned(state), RandSigned(state), RandSigned(state)) * shapeSize;
    }
    else if (shape == 3u) // Cone
    {
        float h = RandUnit(state);
        float t = RandUnit(state) * 6.2831853;
        float rAt = shapeSize.x * h;
        return (right * cos(t) + up * sin(t)) * rAt + forward * (shapeSize.y * h);
    }
    else if (shape == 4u) // Ring
    {
        float t = RandUnit(state) * 6.2831853;
        float outer = shapeSize.x;
        float inner = min(shapeSize.y, outer);
        float r = lerp(inner, outer, RandUnit(state));
        return (right * cos(t) + forward * sin(t)) * r;
    }
    else // Disk
    {
        float t = RandUnit(state) * 6.2831853;
        float outer = shapeSize.x;
        float inner = min(shapeSize.y, outer);
        float r = lerp(inner, outer, sqrt(RandUnit(state)));
        return (right * cos(t) + forward * sin(t)) * r;
    }
}

// Direction within a cone around 'forward' with the given half-angle (radians).
float3 RandDirectionInCone(inout uint state, float3 forward, float3 right, float3 up, float halfAngle)
{
    float angle = halfAngle * RandUnit(state);
    float t = RandUnit(state) * 6.2831853;
    float s = sin(angle);
    return forward * cos(angle) + (right * cos(t) + up * sin(t)) * s;
}

// Module-stack function bodies (filled in by FParticleCompiler).
void SpawnParticleGraph(inout FGPUParticle P, inout uint Seed, float TotalTime, uint SpawnIndex)
{
$PARTICLE_SPAWN_FUNC
}

void UpdateParticleGraph(inout FGPUParticle P, float DeltaTime, float TotalTime)
{
    float LifeRatio = saturate(P.Age / max(P.Lifetime, 0.0001));
$PARTICLE_UPDATE_FUNC
}

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
            // Seed defaults; spawn modules overwrite what they care about.
            uint Seed = PCGHash(Index ^ SimParams.Counts.z);

            P.Position      = SimParams.EmitterPosition.xyz;
            P.Velocity      = float3(0.0, 0.0, 0.0);
            P.Lifetime      = 1.0;
            P.Color         = float4(1.0, 1.0, 1.0, 1.0);
            P.Size          = 0.1;
            P.Age           = 0.0;
            P.Rotation      = 0.0;
            P.RotationSpeed = 0.0;
            P.Seed          = RandUnit(Seed);

            SpawnParticleGraph(P, Seed, SimParams.Timing.y, Index);

            P.Lifetime = max(P.Lifetime, 0.01);

            // Inherit a fraction of the emitter's own motion.
            float3 EmitterVelocity = float3(SimParams.EmitterForward.w,
                                            SimParams.EmitterRight.w,
                                            SimParams.EmitterUp.w);
            P.Velocity += EmitterVelocity * SimParams.NoiseParams.y;
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
            UpdateParticleGraph(P, SimParams.Timing.x, SimParams.Timing.y);
        }
    }

    Particles[Index] = P;
}
