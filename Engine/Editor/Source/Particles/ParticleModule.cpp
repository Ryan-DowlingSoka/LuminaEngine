#include "ParticleModule.h"
#include "Containers/String.h"

namespace Lumina
{
    FString CParticleModule::LocalVar(int32 ModuleIndex, const char* Name)
    {
        return "m" + FString(eastl::to_string(ModuleIndex)) + "_" + Name;
    }

    FString CParticleModule::Lit(float V)
    {
        return FString(eastl::to_string(V));
    }

    FString CParticleModule::Lit(const FVector2& V)
    {
        return "float2(" + Lit(V.x) + ", " + Lit(V.y) + ")";
    }

    FString CParticleModule::Lit(const FVector3& V)
    {
        return "float3(" + Lit(V.x) + ", " + Lit(V.y) + ", " + Lit(V.z) + ")";
    }

    FString CParticleModule::Lit(const FVector4& V)
    {
        return "float4(" + Lit(V.x) + ", " + Lit(V.y) + ", " + Lit(V.z) + ", " + Lit(V.w) + ")";
    }
}
