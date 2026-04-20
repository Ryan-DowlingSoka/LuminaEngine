#pragma once
#include "Containers/Function.h"
#include "Core/Reflection/PropertyChangedEvent.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    enum class EPropertyChangeOp : uint8;
    class FPropertyTable;
    class FStructProperty;
    class CStruct;
    class FPropertyHandle;
    class FArrayProperty;
    class FProperty;
    struct IPropertyTypeCustomization;
    class CObject;
}

namespace Lumina
{

    using FPropertyChangedEventFn = TFunction<void(const FPropertyChangedEvent&)>;

    struct FPropertyChangedEventCallbacks
    {
        CStruct*                Type;
        FPropertyChangedEventFn PreChangeCallback;
        FPropertyChangedEventFn PostChangeCallback;
        FPropertyChangedEventFn StartChangeCallback;
        FPropertyChangedEventFn FinishChangeCallback;

    };
    
    class FPropertyRow
    {
    public:

        FPropertyRow(const FPropertyChangedEventCallbacks& InCallbacks)
            :Callbacks(InCallbacks)
        {}
        
        virtual ~FPropertyRow() = default;
        FPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& Callbacks);

        virtual uint32 GetHeaderWidth() const { return 120; }
        virtual void DrawHeader(float Offset) { }
        virtual void DrawEditor(bool bReadOnly) { }

        virtual bool HasExtraControls() const { return false; }
        virtual void DrawExtraControlsSection() { }
        virtual float GetExtraControlsSectionWidth() { return 0; }

        void DestroyChildren();

        virtual void Update() { }
        void UpdateRow();
        void DrawRow(float Offset, bool bReadOnly);
        virtual void DrawChildren(float ChildOffset, bool bReadOnly);
        bool IsReadOnly() const;

        void SetIsArrayElement(bool bTrue) { bArrayElement = bTrue; }
        bool IsArrayElementProperty() const { return bArrayElement; }

    protected:

        void DispatchChange(EPropertyChangeOp Op);

        
        FPropertyChangedEventCallbacks          Callbacks;
        
        TSharedPtr<IPropertyTypeCustomization>  Customization;
        TSharedPtr<FPropertyHandle>             PropertyHandle;
        FPropertyRow*                           ParentRow = nullptr;
        
        TVector<TUniquePtr<FPropertyRow>>       Children;
        
        EPropertyChangeOp                       ChangeOp = EPropertyChangeOp::None;
        bool                                    bArrayElement = false;
        bool                                    bExpanded = true;
    };

    class FPropertyPropertyRow : public FPropertyRow
    {
    public:

        FPropertyPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& Callbacks);
        void Update() override;
        void DrawHeader(float Offset) override;
        void DrawEditor(bool bReadOnly) override;
        bool HasExtraControls() const override;
        float GetExtraControlsSectionWidth() override;
        void DrawExtraControlsSection() override;

        TSharedPtr<FPropertyHandle> GetPropertyHandle() const { return PropertyHandle; }

    };

    class FArrayPropertyRow : public FPropertyRow
    {
    public:

        FArrayPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& Callbacks);
        void Update() override;
        void DrawHeader(float Offset) override;
        void DrawEditor(bool bReadOnly) override;
        void DrawChildren(float ChildOffset, bool bReadOnly) override;
        void RebuildChildren();
        bool HasExtraControls() const override { return true; }
        float GetExtraControlsSectionWidth() override;
        void DrawExtraControlsSection() override;
        TSharedPtr<FPropertyHandle> GetPropertyHandle() const { return PropertyHandle; }

        bool IsInnerFixedHeight() const;

        FArrayProperty*             ArrayProperty = nullptr;

    private:

        void DrawTruncationRow(float Offset, int HiddenCount);

        bool                        bShowAllElements = false;
    };

    class FStructPropertyRow : public FPropertyRow
    {
    public:
        
        FStructPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks);
        ~FStructPropertyRow() override = default;
        
        void Update() override;
        void DrawHeader(float Offset) override;
        void DrawEditor(bool bReadOnly) override;

        void RebuildChildren();

    private:
        
        FStructProperty*            StructProperty = nullptr;
        TUniquePtr<FPropertyTable>  PropertyTable;
    };
    
    class FCategoryPropertyRow : public FPropertyRow
    {
    public:
        
        FCategoryPropertyRow(void* InObj, const FName& InCategory, const FPropertyChangedEventCallbacks& InCallbacks);

        void AddProperty(const TSharedPtr<FPropertyHandle>& InPropHandle);
        FName GetCategoryName() const { return Category; }

        void DrawHeader(float Offset) override;
        
    private:

        void* OwnerObject = nullptr;
        FName Category;
    };
    
    class FPropertyTable
    {
        friend class FStructPropertyRow;
        
    public:

        FPropertyTable() = default;
        ~FPropertyTable() = default;
        FPropertyTable(void* InObject, CStruct* InType);
        FPropertyTable(const FPropertyTable&) = delete;
        FPropertyTable(FPropertyTable&&) = delete;
        
        bool operator = (const FPropertyTable&) const = delete;
        bool operator = (FPropertyTable&&) = delete;

        void MarkDirty();
        void DrawTree(bool bReadOnly = false);

        CStruct* GetType() const { return Struct; }

        void SetObject(void* InObject, CStruct* StructType);
        void SetPreEditCallback(const FPropertyChangedEventFn& Callback);
        void SetPostEditCallback(const FPropertyChangedEventFn& Callback);
        void SetStartEditCallback(const FPropertyChangedEventFn& Callback);
        void SetFinishEditCallback(const FPropertyChangedEventFn& Callback);

        FCategoryPropertyRow* FindOrCreateCategoryRow(const FName& CategoryName);

        FPropertyChangedEventCallbacks ChangeEventCallbacks;
        
    protected:
        
        void RebuildTree();

    private:
        
        bool                                                bDirty = true;
        TSharedPtr<IPropertyTypeCustomization>              Customization;
        TSharedPtr<FPropertyHandle>                         PropertyHandle;
        CStruct*                                            Struct = nullptr;
        void*                                               Object = nullptr;
        THashMap<FName, TUniquePtr<FCategoryPropertyRow>>   CategoryMap;
        
    };
}
