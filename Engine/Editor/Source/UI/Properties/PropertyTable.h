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
    class FOptionalProperty;
    class FProperty;
    struct IPropertyTypeCustomization;
    class CObject;
    class CClass;
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

        // Width of just this row's own header label (text + collapse arrow if any),
        // not counting indent. Overridden per-row-type so the table can size its
        // header column to fit the widest visible label instead of using a fixed
        // proportional split.
        virtual float GetMeasuredHeaderTextWidth() const { return 0.0f; }

        // Walks this row plus its visible (expanded) children and returns the
        // largest required header width including indent. Used by FPropertyTable
        // to auto-size the header column each frame.
        float ComputeRequiredHeaderWidth(float Offset) const;

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

        // Hook fired right after PropertyHandle->ResetToDefault() runs from
        // the row's reset button or context menu. Container-shaped properties
        // (array, optional, struct) override this to rebuild their child rows
        // so the UI reflects the new structure on the next frame.
        virtual void OnValueResetToDefault() { }
        bool IsReadOnly() const;

        void SetIsArrayElement(bool bTrue) { bArrayElement = bTrue; }
        bool IsArrayElementProperty() const { return bArrayElement; }

        // True only for FCategoryPropertyRow. Used by category rows to find
        // existing nested-category children when fanning out a `Foo|Bar|Baz`
        // metadata path into a tree of rows.
        virtual bool IsCategory() const { return false; }

    protected:

        void DispatchChange(EPropertyChangeOp Op);

        // Centralised reset path used by both the toolbar button and the
        // right-click menu. Writes the default into the container, then
        // re-syncs the customization's cached value from the container so
        // DispatchChange's subsequent UpdatePropertyValue doesn't immediately
        // clobber the reset with the stale widget state.
        void PerformResetToDefault();

        
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
        float GetMeasuredHeaderTextWidth() const override;
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
        float GetMeasuredHeaderTextWidth() const override;
        void DrawChildren(float ChildOffset, bool bReadOnly) override;
        void RebuildChildren();
        bool HasExtraControls() const override { return true; }
        float GetExtraControlsSectionWidth() override;
        void DrawExtraControlsSection() override;
        void OnValueResetToDefault() override { RebuildChildren(); }
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
        float GetMeasuredHeaderTextWidth() const override;
        void OnValueResetToDefault() override { RebuildChildren(); }

        void RebuildChildren();

    private:

        FStructProperty*            StructProperty = nullptr;
        TUniquePtr<FPropertyTable>  PropertyTable;
    };

    /**
     * Editor row for TOptional<T>. The header is the property name; the
     * editor cell hosts a "Set" checkbox plus a small status label. When
     * engaged, the inner T property renders as a single child row beneath,
     * reusing whichever PropertyRow class fits T's type.
     */
    class FOptionalPropertyRow : public FPropertyRow
    {
    public:

        FOptionalPropertyRow(const TSharedPtr<FPropertyHandle>& InPropHandle, FPropertyRow* InParentRow, const FPropertyChangedEventCallbacks& InCallbacks);

        void Update() override;
        void DrawHeader(float Offset) override;
        void DrawEditor(bool bReadOnly) override;
        float GetMeasuredHeaderTextWidth() const override;
        void OnValueResetToDefault() override;

        // Rebuilds the (0-or-1) child row to match the optional's current
        // engaged state. Called after the user toggles the checkbox or when
        // an external mutation flips it underneath us.
        void RebuildChildren();

        TSharedPtr<FPropertyHandle> GetPropertyHandle() const { return PropertyHandle; }

    private:

        FOptionalProperty*  OptionalProperty = nullptr;
        bool                bWasEngaged = false;
    };
    
    class FCategoryPropertyRow : public FPropertyRow
    {
    public:

        FCategoryPropertyRow(void* InObj, const FName& InCategory, const FPropertyChangedEventCallbacks& InCallbacks);

        void AddProperty(const TSharedPtr<FPropertyHandle>& InPropHandle);

        // Returns the existing child category row with this name if one was
        // already created for this row, else creates a new one and appends it
        // to Children. Used by FPropertyTable::RebuildTree when expanding a
        // `Outer|Inner` category path one segment at a time.
        FCategoryPropertyRow* FindOrCreateChildCategory(const FName& InCategory);

        FName GetCategoryName() const { return Category; }
        bool IsCategory() const override { return true; }

        void DrawHeader(float Offset) override;
        float GetMeasuredHeaderTextWidth() const override;

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

        // Auto-derives Type from the CObject's class and DefaultObject from
        // the class CDO. Use this for any details panel rooted at a CObject:
        // it enables the "modified" indicator and reset-to-default button.
        explicit FPropertyTable(CObject* InObject);

        // Explicit default-object form, for callers that have a hand-built
        // default to compare against (e.g. nested struct rebuild paths).
        FPropertyTable(void* InObject, CStruct* InType, void* InDefaultObject);

        FPropertyTable(const FPropertyTable&) = delete;
        FPropertyTable(FPropertyTable&&) = delete;
        
        bool operator = (const FPropertyTable&) const = delete;
        bool operator = (FPropertyTable&&) = delete;

        void MarkDirty();
        void DrawTree(bool bReadOnly = false);

        CStruct* GetType() const { return Struct; }
        void* GetObject() const { return Object; }
        void* GetDefaultObject() const { return DefaultObject; }

        void SetObject(void* InObject, CStruct* StructType);
        void SetObject(void* InObject, CStruct* StructType, void* InDefaultObject);
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
        // Parallel pointer used to resolve a property's default value when
        // computing diff-from-default and reset-to-default. Null when no
        // default is plumbed in (e.g. plain struct details with no CDO).
        void*                                               DefaultObject = nullptr;
        THashMap<FName, TUniquePtr<FCategoryPropertyRow>>   CategoryMap;
        
    };
}
