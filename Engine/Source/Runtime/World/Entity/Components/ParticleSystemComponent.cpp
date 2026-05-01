#include "pch.h"
#include "ParticleSystemComponent.h"

namespace Lumina
{
    static glm::vec4 PromoteToVec4(const glm::vec2& V) { return glm::vec4(V, 0.0f, 0.0f); }
    static glm::vec4 PromoteToVec4(const glm::vec3& V) { return glm::vec4(V, 0.0f); }
    static glm::vec4 PromoteToVec4(const glm::vec4& V) { return V; }

    const FParticleParameter* SParticleSystemComponent::FindParameter(const FName& Name) const
    {
        if (Name.IsNone())
        {
            return nullptr;
        }

        for (const FParticleParameter& Override : ParameterOverrides)
        {
            if (Override.Name == Name)
            {
                return &Override;
            }
        }

        if (ParticleSystem)
        {
            return ParticleSystem->FindUserParameter(Name);
        }

        return nullptr;
    }

    FParticleParameter* SParticleSystemComponent::GetOrCreateOverride(FName Name, EParticleParameterType ExpectedType)
    {
        if (Name.IsNone())
        {
            return nullptr;
        }

        for (FParticleParameter& Override : ParameterOverrides)
        {
            if (Override.Name == Name)
            {
                if (Override.Type != ExpectedType)
                {
                    return nullptr;
                }
                return &Override;
            }
        }

        if (ParticleSystem)
        {
            const FParticleParameter* AssetParam = ParticleSystem->FindUserParameter(Name);
            if (AssetParam && AssetParam->Type != ExpectedType)
            {
                return nullptr;
            }
        }

        FParticleParameter NewOverride;
        NewOverride.Name = Name;
        NewOverride.Type = ExpectedType;
        ParameterOverrides.push_back(NewOverride);
        return &ParameterOverrides.back();
    }

    float SParticleSystemComponent::GetFloat(const FName& Name, float Default) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Float) ? Param->Scalar : Default;
    }

    int32 SParticleSystemComponent::GetInt(const FName& Name, int32 Default) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Int) ? Param->Integer : Default;
    }

    bool SParticleSystemComponent::GetBool(const FName& Name, bool Default) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Bool) ? Param->Boolean : Default;
    }

    glm::vec2 SParticleSystemComponent::GetVec2(const FName& Name) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Vec2) ? glm::vec2(Param->Vector) : glm::vec2(0.0f);
    }

    glm::vec3 SParticleSystemComponent::GetVec3(const FName& Name) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Vec3) ? glm::vec3(Param->Vector) : glm::vec3(0.0f);
    }

    glm::vec4 SParticleSystemComponent::GetVec4(const FName& Name) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Vec4) ? Param->Vector : glm::vec4(0.0f);
    }

    glm::vec4 SParticleSystemComponent::GetColor(const FName& Name) const
    {
        const FParticleParameter* Param = FindParameter(Name);
        return (Param && Param->Type == EParticleParameterType::Color) ? Param->Vector : glm::vec4(0.0f);
    }

    void SParticleSystemComponent::SetFloat(const FName& Name, float Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Float))
        {
            P->Scalar = Value;
        }
    }

    void SParticleSystemComponent::SetInt(const FName& Name, int32 Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Int))
        {
            P->Integer = Value;
        }
    }

    void SParticleSystemComponent::SetBool(const FName& Name, bool Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Bool))
        {
            P->Boolean = Value;
        }
    }

    void SParticleSystemComponent::SetVec2(const FName& Name, glm::vec2 Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Vec2))
        {
            P->Vector = PromoteToVec4(Value);
        }
    }

    void SParticleSystemComponent::SetVec3(const FName& Name, glm::vec3 Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Vec3))
        {
            P->Vector = PromoteToVec4(Value);
        }
    }

    void SParticleSystemComponent::SetVec4(const FName& Name, glm::vec4 Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Vec4))
        {
            P->Vector = Value;
        }
    }

    void SParticleSystemComponent::SetColor(const FName& Name, glm::vec4 Value)
    {
        if (FParticleParameter* P = GetOrCreateOverride(Name, EParticleParameterType::Color))
        {
            P->Vector = Value;
        }
    }

    void SParticleSystemComponent::ResetParameter(const FName& Name)
    {
        for (auto It = ParameterOverrides.begin(); It != ParameterOverrides.end(); ++It)
        {
            if (It->Name == Name)
            {
                ParameterOverrides.erase(It);
                return;
            }
        }
    }
}
