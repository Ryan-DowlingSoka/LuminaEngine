#pragma once

#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "GameplayTags/GameplayTag.h"

namespace Lumina
{
    // Draws an FGameplayTag property as a searchable dropdown over the registered tags, with a "+" button to
    // author a new tag (interned + persisted to CGameplayTagsSettings). FGameplayTagContainer reuses this
    // automatically: its Tags array renders each element with this picker.
    class FGameplayTagPropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FGameplayTagPropertyCustomization> MakeInstance()
        {
            return MakeShared<FGameplayTagPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        FGameplayTag Value;
        char NewTagBuffer[128] = {};
    };
}
