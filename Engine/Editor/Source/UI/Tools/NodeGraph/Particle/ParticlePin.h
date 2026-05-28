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
        void SetDefaultFloat3(FVector3 V) { DefaultFloat3 = V; }
        void SetDefaultFloat4(FVector4 V) { DefaultFloat4 = V; }

        float GetDefaultFloat() const { return DefaultFloat; }
        const FVector3& GetDefaultFloat3() const { return DefaultFloat3; }
        const FVector4& GetDefaultFloat4() const { return DefaultFloat4; }

    private:

        EParticlePinType    PinType = EParticlePinType::Float;
        float               DefaultFloat = 0.0f;
        FVector3           DefaultFloat3 = FVector3(0.0f);
        FVector4           DefaultFloat4 = FVector4(1.0f);
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
