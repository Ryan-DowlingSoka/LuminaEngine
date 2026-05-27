#pragma once
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Memory/SmartPtr.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    // Customization for a uint32 property marked PROPERTY(Entity). Instead of a raw
    // number field it draws a searchable picker listing the entities in the active world
    // context (see FScopedEntityPropertyContext), Unreal-actor-reference style. The stored
    // value is the entity's integral id; an unset reference is entt::null's integral.
    class FEntityPropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FEntityPropertyCustomization> MakeInstance()
        {
            return MakeShared<FEntityPropertyCustomization>();
        }

        // Cancels an in-flight eyedropper pick if this picker is torn down mid-pick
        // (e.g. the details panel rebuilds), so the viewport doesn't stay in pick mode.
        ~FEntityPropertyCustomization();

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        uint32 CachedValue = 0;
    };
}
