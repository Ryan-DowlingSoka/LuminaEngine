// -----------------------------------------------------------------------------
//  ParticleSimulate.slang
//  Default data-driven particle simulation compute shader. Every module is
//  driven by the FParticleSimParams uniform buffer, so this single shader
//  handles explosions, smoke, sparks, rain, etc. without recompilation.
//
//  Layout must stay in sync with:
//    - CParticleSystem (asset)
//    - FForwardRenderScene particle passes (ForwardRenderScene.cpp)
//    - ParticleVertex.slang (consumes FGPUParticle)
// -----------------------------------------------------------------------------

// Emitter shape enums (must match EParticleEmitterShape)
static const uint EMITTER_SHAPE_POINT  = 0u;
static const uint EMITTER_SHAPE_SPHERE = 1u;
static const uint EMITTER_SHAPE_BOX    = 2u;
static const uint EMITTER_SHAPE_CONE   = 3u;
static const uint EMITTER_SHAPE_RING   = 4u;
static const uint EMITTER_SHAPE_DISK   = 5u;

// Velocity mode enums (must match EParticleVelocityMode)
static const uint VELOCITY_MODE_EXPLICIT = 0u;
static const uint VELOCITY_MODE_RADIAL   = 1u;

// Sim flags (bitmask in Counts.w)
static const uint SIM_FLAG_LOOP         = (1u << 0);
static const uint SIM_FLAG_BURST_PENDING = (1u << 1);

// -----------------------------------------------------------------------------
//  Buffer layout
// -----------------------------------------------------------------------------

// 64-byte particle. Do not change stride without updating ForwardRenderScene.
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

// 256-byte uniform block. Kept vec4-aligned for safe std140 layout.
struct FParticleSimParams
{
    float4  EmitterPosition;     // xyz = world position
    float4  EmitterForward;      // xyz = forward (normalized)
    float4  EmitterRight;        // xyz = right   (normalized)
    float4  EmitterUp;           // xyz = up      (normalized)

    uint4   Counts;              // x=MaxParticles, y=SpawnCount, z=FrameSeed, w=SimFlags
    uint4   Modes;               // x=Shape, y=VelocityMode, z=reserved, w=reserved

    float4  ShapeSize;           // xyz dims; w=cone half-angle (radians)

    float4  VelocityMin;         // xyz
    float4  VelocityMax;         // xyz

    float4  SpeedAndLifetime;    // x=speedMin, y=speedMax, z=lifeMin, w=lifeMax
    float4  Gravity;             // xyz=gravity, w=drag

    float4  StartColor;
    float4  EndColor;

    float4  SizeRange;           // xy=start(min,max); zw=end(min,max)
    float4  RotationRange;       // xy=rot(min,max); zw=rotSpeed(min,max)

    float4  NoiseStrength;       // xyz=strength; w=scale
    float4  NoiseParams;         // x=speed; yzw reserved

    float4  Timing;              // x=DeltaTime, y=TotalTime, z=SystemAge, w=unused
};

[[vk::binding(0, 0)]] RWStructuredBuffer<FGPUParticle>    Particles;
[[vk::binding(1, 0)]] ConstantBuffer<FParticleSimParams>  SimParams;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>            SpawnCounter;   // scratch counter, sized 4 bytes

// -----------------------------------------------------------------------------
//  PRNG / hashing helpers
// -----------------------------------------------------------------------------

uint WangHash(uint x)
{
    x = (x ^ 61u) ^ (x >> 16u);
    x *= 9u;
    x = x ^ (x >> 4u);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x;
}

uint PCGHash(uint state)
{
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandUnit(inout uint state)
{
    state = PCGHash(state);
    return float(state) / 4294967295.0;
}

// [-1, 1]
float RandSigned(inout uint state)
{
    return RandUnit(state) * 2.0 - 1.0;
}

float3 RandInUnitSphere(inout uint state)
{
    // Reject until inside sphere.
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float3 p = float3(RandSigned(state), RandSigned(state), RandSigned(state));
        if (dot(p, p) <= 1.0) return p;
    }
    // Fallback normalized point on sphere surface.
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

// -----------------------------------------------------------------------------
//  Cheap 3D value noise - adequate for particle turbulence without sampling
//  a 3D texture. Output in [-1, 1].
// -----------------------------------------------------------------------------

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

// Pseudo-curl: sample noise at three offsets to build a divergence-free-ish field.
float3 CurlishNoise(float3 p, float time, float scale, float speed)
{
    float3 q = p * scale + float3(0.0, time * speed, 0.0);
    const float e = 0.15;
    float3 d;
    d.x = ValueNoise(q + float3(31.3, 0.0, 0.0))
        - ValueNoise(q - float3(31.3, 0.0, 0.0));
    d.y = ValueNoise(q + float3(0.0, 47.7, 0.0))
        - ValueNoise(q - float3(0.0, 47.7, 0.0));
    d.z = ValueNoise(q + float3(0.0, 0.0, 19.1))
        - ValueNoise(q - float3(0.0, 0.0, 19.1));
    return d * (1.0 / (2.0 * e));
}

// -----------------------------------------------------------------------------
//  Emitter shape sampling
// -----------------------------------------------------------------------------

float3 SampleEmitterShape(inout uint state, uint shape, float3 shapeSize, float coneAngle,
                          float3 forward, float3 right, float3 up)
{
    if (shape == EMITTER_SHAPE_POINT)
    {
        return float3(0, 0, 0);
    }
    else if (shape == EMITTER_SHAPE_SPHERE)
    {
        return RandInUnitSphere(state) * shapeSize.x;
    }
    else if (shape == EMITTER_SHAPE_BOX)
    {
        float3 r = float3(RandSigned(state), RandSigned(state), RandSigned(state));
        return r * shapeSize;
    }
    else if (shape == EMITTER_SHAPE_CONE)
    {
        // Cone oriented along forward; base radius = x, height = y.
        float h = RandUnit(state);
        float t = RandUnit(state) * 6.2831853;
        float rAt = shapeSize.x * h;
        float3 offset = (right * cos(t) + up * sin(t)) * rAt + forward * (shapeSize.y * h);
        return offset;
    }
    else if (shape == EMITTER_SHAPE_RING)
    {
        float t = RandUnit(state) * 6.2831853;
        float outer = shapeSize.x;
        float inner = min(shapeSize.y, outer);
        float r = lerp(inner, outer, RandUnit(state));
        return (right * cos(t) + forward * sin(t)) * r;
    }
    else /* DISK */
    {
        float t = RandUnit(state) * 6.2831853;
        float outer = shapeSize.x;
        float inner = min(shapeSize.y, outer);
        float r = lerp(inner, outer, sqrt(RandUnit(state)));
        return (right * cos(t) + forward * sin(t)) * r;
    }
}

float3 ComputeInitialVelocity(inout uint state, uint mode, uint shape, float3 spawnOffset,
                              float4 velMin, float4 velMax, float2 speedRange,
                              float coneAngle, float3 forward, float3 right, float3 up)
{
    if (mode == VELOCITY_MODE_RADIAL)
    {
        float3 dir;
        float len = length(spawnOffset);
        if (len > 1e-5)
        {
            dir = spawnOffset / len;
        }
        else
        {
            // Degenerate (point shape): sample a random direction, biased by cone if applicable.
            if (shape == EMITTER_SHAPE_CONE)
            {
                float angle = coneAngle * RandUnit(state);
                float t = RandUnit(state) * 6.2831853;
                float s = sin(angle);
                dir = forward * cos(angle) + (right * cos(t) + up * sin(t)) * s;
            }
            else
            {
                dir = RandOnUnitSphere(state);
            }
        }
        float speed = lerp(speedRange.x, speedRange.y, RandUnit(state));
        return dir * speed;
    }

    // Explicit: independent per-axis lerp.
    float3 r = float3(RandUnit(state), RandUnit(state), RandUnit(state));
    return lerp(velMin.xyz, velMax.xyz, r);
}

// -----------------------------------------------------------------------------
//  Spawn / Simulate
// -----------------------------------------------------------------------------

void SpawnParticle(uint Index, inout FGPUParticle P)
{
    uint seed = WangHash(Index * 73856093u ^ SimParams.Counts.z);

    uint shape = SimParams.Modes.x;
    uint velMode = SimParams.Modes.y;

    float3 fwd   = SimParams.EmitterForward.xyz;
    float3 rgt   = SimParams.EmitterRight.xyz;
    float3 up_   = SimParams.EmitterUp.xyz;

    float3 spawnOffset = SampleEmitterShape(seed, shape,
                                            SimParams.ShapeSize.xyz, SimParams.ShapeSize.w,
                                            fwd, rgt, up_);

    float3 spawnPos = SimParams.EmitterPosition.xyz + spawnOffset;

    float3 vel = ComputeInitialVelocity(seed, velMode, shape, spawnOffset,
                                        SimParams.VelocityMin, SimParams.VelocityMax,
                                        SimParams.SpeedAndLifetime.xy,
                                        SimParams.ShapeSize.w,
                                        fwd, rgt, up_);

    float lifetime = lerp(SimParams.SpeedAndLifetime.z, SimParams.SpeedAndLifetime.w, RandUnit(seed));
    float size     = lerp(SimParams.SizeRange.x,        SimParams.SizeRange.y,        RandUnit(seed));
    float rotation = lerp(SimParams.RotationRange.x,    SimParams.RotationRange.y,    RandUnit(seed));
    float rotSpeed = lerp(SimParams.RotationRange.z,    SimParams.RotationRange.w,    RandUnit(seed));
    float particleSeed = RandUnit(seed);

    // Emitter velocity inheritance: EmitterVelocity is packed in the .w components
    // of the emitter basis vectors; NoiseParams.y is the 0-1 inheritance factor.
    float3 EmitterVelocity = float3(SimParams.EmitterForward.w,
                                    SimParams.EmitterRight.w,
                                    SimParams.EmitterUp.w);
    vel += EmitterVelocity * SimParams.NoiseParams.y;

    P.Position      = spawnPos;
    P.Age           = 0.0;
    P.Velocity      = vel;
    P.Lifetime      = max(lifetime, 0.01);
    P.Color         = SimParams.StartColor;
    P.Size          = size;
    P.Rotation      = radians(rotation);
    P.RotationSpeed = radians(rotSpeed);
    P.Seed          = particleSeed;
}

void SimulateParticle(inout FGPUParticle P, float DeltaTime, float TotalTime)
{
    // Gravity
    P.Velocity += SimParams.Gravity.xyz * DeltaTime;

    // Noise acceleration
    float3 noiseStrength = SimParams.NoiseStrength.xyz;
    if (any(noiseStrength > 0.0))
    {
        float3 turbulence = CurlishNoise(P.Position,
                                         TotalTime,
                                         SimParams.NoiseStrength.w,
                                         SimParams.NoiseParams.x);
        P.Velocity += turbulence * noiseStrength * DeltaTime;
    }

    // Exponential drag (1st order). Keeps behavior framerate-independent.
    float drag = SimParams.Gravity.w;
    if (drag > 0.0)
    {
        P.Velocity *= exp(-drag * DeltaTime);
    }

    P.Position += P.Velocity * DeltaTime;
    P.Rotation += P.RotationSpeed * DeltaTime;

    // Color over life
    float lifeT = saturate(P.Age / P.Lifetime);
    P.Color = lerp(SimParams.StartColor, SimParams.EndColor, lifeT);

    // Size over life (random end per-particle but deterministic via Seed)
    float endSize = lerp(SimParams.SizeRange.z, SimParams.SizeRange.w, P.Seed);
    float startSize = lerp(SimParams.SizeRange.x, SimParams.SizeRange.y, P.Seed);
    P.Size = lerp(startSize, endSize, lifeT);
}

// -----------------------------------------------------------------------------
//  Entry point. 64 threads per group matches wavefront widths on AMD/NVIDIA.
// -----------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Index = DispatchThreadID.x;
    uint MaxParticles = SimParams.Counts.x;
    if (Index >= MaxParticles)
    {
        return;
    }

    FGPUParticle P = Particles[Index];

    bool bAlive = (P.Lifetime > 0.0) && (P.Age < P.Lifetime);

    if (!bAlive)
    {
        // Race for a spawn slot: first SpawnCount dead particles (in thread order) spawn this frame.
        uint myAlloc;
        InterlockedAdd(SpawnCounter[0], 1u, myAlloc);
        if (myAlloc < SimParams.Counts.y)
        {
            SpawnParticle(Index, P);
        }
        else
        {
            // Keep particle flagged dead
            P.Lifetime = 0.0;
            P.Age      = 0.0;
            P.Color    = float4(0, 0, 0, 0);
            P.Size     = 0.0;
        }
    }
    else
    {
        float dt = SimParams.Timing.x;
        P.Age += dt;
        if (P.Age >= P.Lifetime)
        {
            P.Lifetime = 0.0;
            P.Age      = 0.0;
            P.Color    = float4(0, 0, 0, 0);
            P.Size     = 0.0;
        }
        else
        {
            SimulateParticle(P, dt, SimParams.Timing.y);
        }
    }

    Particles[Index] = P;
}
