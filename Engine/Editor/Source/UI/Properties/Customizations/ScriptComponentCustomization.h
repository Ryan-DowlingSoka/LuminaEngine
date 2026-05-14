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
        
        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;

        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

        ImGuiTextFilter SearchFilter;

    private:

        // The component is mutated directly during draw, so discrete edits (clear / pick / drop /
        // refresh) are captured here and replayed from UpdatePropertyValue — which runs after
        // BeginTransaction — so the undo snapshot captures the pre-change state. Started is emitted
        // on the change frame and Finished the next, wrapping the edit in one transaction.
        TFunction<void()> PendingMutation;
        bool bFinishPending = false;
    };
}
