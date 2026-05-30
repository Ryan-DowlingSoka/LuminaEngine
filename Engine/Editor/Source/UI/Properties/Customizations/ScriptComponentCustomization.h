#pragma once

#include "ImGuiDrawUtils.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "World/Entity/Components/ScriptComponent.h"


namespace Lumina
{
    class FScriptComponentPropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FScriptComponentPropertyCustomization> MakeInstance();
        
        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;

        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

        ImGuiTextFilter SearchFilter;

    private:

        // Discrete edits (clear/pick/drop/refresh) are captured here and replayed from
        // UpdatePropertyValue (after BeginTransaction) so the undo snapshot is pre-change.
        TFunction<void()> PendingMutation;
        bool bFinishPending = false;
    };
}
