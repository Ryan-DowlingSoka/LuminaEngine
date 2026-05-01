#pragma once

#include "Assets/AssetTypes/ParticleSystem/ParticleSystem.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"

namespace Lumina
{
    /**
     * Inline editor for a single FParticleParameter row inside the asset's UserParameters
     * array. Lays out [Name] [Type] [typed value editor] on one line, Niagara-style.
     */
    class FParticleParameterCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FParticleParameterCustomization> MakeInstance();

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

    private:

        FParticleParameter Value;
        char NameBuffer[128] = {};
    };
}
