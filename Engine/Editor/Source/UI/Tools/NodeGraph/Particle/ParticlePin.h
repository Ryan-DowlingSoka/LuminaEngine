#pragma once

#include "UI/Tools/NodeGraph/EdNodeGraphPin.h"
#include "ParticlePin.generated.h"

namespace Lumina
{
    enum class EParticlePinType : uint8
    {
        Float,
        Float3,
        Float4,
    };

    REFLECT()
    class CParticleInput : public CEdNodeGraphPin
    {
        GENERATED_BODY()
    public:

        float DrawPin() override;

        void SetPinType(EParticlePinType InType) { PinType = InType; }
        EParticlePinType GetPinType() const { return PinType; }

        void SetDefaultFloat(float V) { DefaultFloat = V; }
        void SetDefaultFloat3(glm::vec3 V) { DefaultFloat3 = V; }
        void SetDefaultFloat4(glm::vec4 V) { DefaultFloat4 = V; }

        float GetDefaultFloat() const { return DefaultFloat; }
        const glm::vec3& GetDefaultFloat3() const { return DefaultFloat3; }
        const glm::vec4& GetDefaultFloat4() const { return DefaultFloat4; }

    private:

        EParticlePinType    PinType = EParticlePinType::Float;
        float               DefaultFloat = 0.0f;
        glm::vec3           DefaultFloat3 = glm::vec3(0.0f);
        glm::vec4           DefaultFloat4 = glm::vec4(1.0f);
    };

    REFLECT()
    class CParticleOutput : public CEdNodeGraphPin
    {
        GENERATED_BODY()
    public:

        float DrawPin() override;

        void SetPinType(EParticlePinType InType) { PinType = InType; }
        EParticlePinType GetPinType() const { return PinType; }

    private:

        EParticlePinType PinType = EParticlePinType::Float;
    };
}
