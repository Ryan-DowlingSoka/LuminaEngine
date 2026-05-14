#pragma once

#include "ImGuiDrawUtils.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Renderer/CustomPrimitiveData.h"


namespace Lumina
{
    class FCustomPrimDataPropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FCustomPrimDataPropertyCustomization> MakeInstance();
        
        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;

        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

        SCustomPrimitiveData Value;
        ImGuiTextFilter SearchFilter;

        // Edits are buffered in Value and only written to the container via UpdatePropertyValue.
        // Emit Started on the change frame and Finished the next so the edit is wrapped in an
        // undo transaction instead of a bare Updated that never opens or commits one. A drag
        // across many frames folds into the single open transaction via the Updated branch.
        bool bFinishPending = false;
    };
}
