#pragma once
#include "Containers/Function.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    class FProperty;

    class RUNTIME_API FPropertyHandle
    {
    public:

        FPropertyHandle(void* InContainerPtr, FProperty* InProperty, int64 InIndex = 0);
        FPropertyHandle(void* InContainerPtr, void* InDefaultContainerPtr, FProperty* InProperty, int64 InIndex = 0);

        /** False if no default container plumbed (e.g. struct edited without a CDO). */
        bool DiffersFromDefault() const;

        /** No-op without default. Caller handles change notifications. */
        void ResetToDefault();

        bool HasDefault() const { return DefaultContainerPtr != nullptr; }

        void* ContainerPtr;
        void* DefaultContainerPtr = nullptr;
        FProperty* Property;
        int64 Index = 0;
    };

    enum class RUNTIME_API EPropertyChangeOp : uint8
    {
        None,
        Updated,
        Started,
        Finished,
    };
    
    struct RUNTIME_API IPropertyTypeCustomization : TSharedFromThis<IPropertyTypeCustomization>
    {
    public:

        virtual EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) = 0;
        
        EPropertyChangeOp UpdateAndDraw(const TSharedPtr<FPropertyHandle>& Property, bool bReadOnly);

        virtual void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) = 0;

        virtual void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) = 0;
        
    };
    
    class RUNTIME_API FPropertyCustomizationRegistry
    {
    public:
        using PropertyCustomizationRegisterFn = TFunction<TSharedPtr<IPropertyTypeCustomization>()>;

        
        void RegisterPropertyCustomization(const FName& Name, PropertyCustomizationRegisterFn Callback);
        void UnregisterPropertyCustomization(const FName& Name);

        bool IsTypeRegistered(const FName& Name);
        
        TSharedPtr<IPropertyTypeCustomization> GetPropertyCustomizationForType(const FName& Type);
    

    private:
        
        THashMap<FName, PropertyCustomizationRegisterFn> RegisteredProperties;
    };
}
