#pragma once

#include "Core/Object/Field.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Serialization/Structured/StructuredArchive.h"
#include "Metadata/PropertyMetadata.h"
#include "Platform/GenericPlatform.h"


namespace Lumina
{
    struct FPropertyParams;
    class IStructuredArchive;
}

namespace Lumina
{

    #define DECLARE_FPROPERTY(Type) \
    static EPropertyTypeFlags StaticType() { return Type; } \
    virtual EPropertyTypeFlags GetType() override { return StaticType(); }
    
    class FProperty : public FField
    {
    public:
        
        FProperty(const FFieldOwner& InOwner, const FPropertyParams* Params)
            :FField(InOwner)
        {
            Offset      = Params->Offset;
            Name        = Params->Name;
            Owner       = InOwner;
            TypeFlags   = Params->TypeFlags;
            Flags       = Params->PropertyFlags;

            TypeName = PropertyTypeToString(TypeFlags);
            Init();
        }
        
        LE_NO_COPYMOVE(FProperty);
        
        // Adds self to owner.
        void Init();

        RUNTIME_API size_t GetElementSize() const { return ElementSize; }
        RUNTIME_API void SetElementSize(size_t Size) { ElementSize = (uint32)Size; }
        RUNTIME_API virtual EPropertyTypeFlags GetType() { return TypeFlags; }

        template<typename ValueType>
        ValueType* SetValuePtr(void* ContainerPtr, const ValueType& Value, int64 ArrayIndex = 0) const
        {
            ValueType* ValuePtr = GetValuePtr<ValueType>(ContainerPtr, ArrayIndex);
            *ValuePtr = Value;
            return ValuePtr;
        }

        /** Gets the cast internal value type by an offset, UB if type is not correct */
        template<typename ValueType>
        requires !eastl::is_pointer_v<ValueType>
        ValueType* GetValuePtr(void* ContainerPtr, int64 ArrayIndex = 0) const
        {
            return static_cast<ValueType*>(GetValuePtrInternal(ContainerPtr, ArrayIndex));
        }

        /** Gets the cast internal value type by an offset, UB if type is not correct */
        template<typename ValueType>
        requires !eastl::is_pointer_v<ValueType>
        const ValueType* GetValuePtr(const void* ContainerPtr, int64 ArrayIndex = 0) const
        {
            return static_cast<ValueType*>(GetValuePtrInternal(const_cast<void*>(ContainerPtr), ArrayIndex));
        }

        template<typename ValueType>
        void SetValue(void* InContainer, const ValueType& InValue, int64 ArrayIndex = 0) const
        {
            if (!HasSetter())
            {
                SetValuePtr<ValueType>(InContainer, InValue, ArrayIndex);
            }
            else
            {
                CallSetter(InContainer, &InValue);
            }
        }

        template<typename ValueType>
        void GetValue(void const* InContainer, ValueType* OutValue, int64 ArrayIndex = 0) const
        {
            if (!HasGetter())
            {
                const ValueType* Src = GetValuePtr<ValueType>(InContainer, ArrayIndex);
                *OutValue = *Src;
            }
            else
            {
                CallGetter(InContainer, OutValue);
            }
        }

        virtual void Serialize(FArchive& Ar, void* Value) { }
        virtual void SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults = nullptr) { }

        RUNTIME_API bool IsA(EPropertyTypeFlags Flag) const { return TypeFlags == Flag; }
        
        const FName& GetTypeName() const;
        
        NODISCARD bool IsReadOnly()     const       { return EnumHasAnyFlags(Flags, EPropertyFlags::ReadOnly); }
        NODISCARD bool ShouldSerialize()const       { return !EnumHasAnyFlags(Flags, EPropertyFlags::NoSerialize); }
        NODISCARD bool IsEditable()     const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Editable); }
        NODISCARD bool IsConst()        const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Const); }
        NODISCARD bool IsInner()        const       { return EnumHasAnyFlags(Flags, EPropertyFlags::SubField); }
        NODISCARD bool IsProtected()    const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Protected); }
        NODISCARD bool IsPrivate()      const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Private); }
        NODISCARD bool IsScript()       const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Script); }
        NODISCARD bool IsVisible()      const       { return EnumHasAnyFlags(Flags, EPropertyFlags::ReadOnly | EPropertyFlags::Editable); }
        NODISCARD bool IsTrivial()      const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Trivial); }
        NODISCARD bool IsBuiltin()      const       { return EnumHasAnyFlags(Flags, EPropertyFlags::Builtin); }
        NODISCARD bool CanBeBulkSerialized() const  { return EnumHasAnyFlags(Flags, EPropertyFlags::BulkSerialize); }

        
        FName GetMetadata(const FName& Key) const { return Metadata.GetMetadata(Key); }
        bool HasMetadata(const FName& Key) const { return Metadata.HasMetadata(Key); }

        void OnMetadataFinalized();
        static FString MakeDisplayNameFromName(EPropertyTypeFlags TypeFlags, const FName& InName);

        RUNTIME_API virtual FString ToString(const void* Data) const { return "<unknown>"; }
        
        virtual bool HasSetter() const { return false; }

        virtual bool HasGetter() const { return false; }

        virtual bool HasSetterOrGetter() const { return false; }

        virtual void CallSetter(void* Container, const void* InValue) const;

        virtual void CallGetter(const void* Container, void* OutValue) const;

        
    private:

        RUNTIME_API void* GetValuePtrInternal(void* ContainerPtr, int64 ArrayIndex) const;
        
    public:
        
        /** Any attached metadata of the property */
        FMetaDataPair       Metadata;
        
        /** Typename of the element */
        FName               TypeName;
        
        /** Size of the internal element type */
        uint32              ElementSize;

        /** Flags this property has (ReadOnly, Transient, etc.) */
        EPropertyFlags      Flags;
        
        /** Specifies the type of property this is */
        EPropertyTypeFlags  TypeFlags;
    };

    template <typename PropertyBaseClass>
    class TPropertyWithSetterAndGetter : public PropertyBaseClass
    {
    public:
        
        template <typename PropertyCodegenParams>
        TPropertyWithSetterAndGetter(const FFieldOwner& InOwner, const PropertyCodegenParams* Prop)
            : PropertyBaseClass(InOwner, Prop)
            , SetterFunc(Prop->SetterFunc)
            , GetterFunc(Prop->GetterFunc)
        {
        }

        virtual bool HasSetter() const override
        {
            return !!SetterFunc;
        }

        virtual bool HasGetter() const override
        {
            return !!GetterFunc;
        }

        virtual bool HasSetterOrGetter() const override
        {
            return !!SetterFunc || !!GetterFunc;
        }

        virtual void CallSetter(void* Container, const void* InValue) const override
        {
            if (SetterFunc == nullptr)
            {
                LOG_CRITICAL("Calling a setter but the property has no setter defined.");
                return;
            }
            SetterFunc(Container, InValue);
        }

        virtual void CallGetter(const void* Container, void* OutValue) const override
        {
            if (GetterFunc == nullptr)
            {
                LOG_CRITICAL("Calling a getter but the property has no getter defined.");
            }
            GetterFunc(Container, OutValue);
        }

    protected:

        SetterFuncPtr SetterFunc = nullptr;
        GetterFuncPtr GetterFunc = nullptr;
    };

    class FNumericProperty : public FProperty
    {
    public:

        FNumericProperty(const FFieldOwner& InOwner, const FPropertyParams* Params)
            :FProperty(InOwner, Params)
        {}

        RUNTIME_API virtual void SetIntPropertyValue(void* Data, uint64 Value) const { }
        RUNTIME_API virtual void SetIntPropertyValue(void* Data, int64 Value) const { }
        
        RUNTIME_API virtual int64 GetSignedIntPropertyValue(void const* Data) const { return 0; }
        RUNTIME_API virtual int64 GetSignedIntPropertyValue_InContainer(void const* Container) const { return 0; }
        
        RUNTIME_API virtual uint64 GetUnsignedIntPropertyValue(void const* Data) const { return 0; }
        RUNTIME_API virtual uint64 GetUnsignedIntPropertyValue_InContainer(void const* Container) const { return 0; }
        
    };
    
    template<typename TCPPType>
    class TPropertyTypeLayout
    {
    public:

        enum : uint8
        {
            Size = sizeof(TCPPType),
            Alignment = alignof(TCPPType)
        };

        /** Convert the address of a value of the property to the proper type */
        static const TCPPType* GetPropertyValuePtr(const void* Ptr)
        {
            return static_cast<const TCPPType*>(Ptr);
        }

        /** Convert the address of a value of the property to the proper type */
        static TCPPType* GetPropertyValuePtr(void* Ptr)
        {
            return static_cast<TCPPType*>(Ptr);
        }

        /** Get the value of the property from an address */
        static TCPPType const& GetPropertyValue(void const* A)
        {
            return *GetPropertyValuePtr(A);
        }

        /** Set the value of a property at an address */
        static void SetPropertyValue(void* Ptr, const TCPPType& Value)
        {
            *GetPropertyValuePtr(Ptr) = Value;
        }
        
    };
    
    template<typename TBacking, typename TCPPType>
    class TProperty : public TBacking
    {
    public:

        using TTypeInfo = TPropertyTypeLayout<TCPPType>;
        
        TProperty(FFieldOwner InOwner, const FPropertyParams* Params)
            :TBacking(InOwner, Params)
        {
            this->ElementSize = TTypeInfo::Size;
        }

        virtual void Serialize(FArchive& Ar, void* Value) override
        {
            Ar << *TTypeInfo::GetPropertyValuePtr(Value);
        }
        
        virtual void SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults = nullptr) override
        {
        }
        
    };


    template<typename TCPPType>
    requires eastl::is_arithmetic_v<TCPPType>
    class TProperty_Numeric : public TProperty<FNumericProperty, TCPPType>
    {
    public:
        
        using TTypeInfo = TPropertyTypeLayout<TCPPType>;
        using Super = TProperty<FNumericProperty, TCPPType>;

        TProperty_Numeric(FFieldOwner InOwner, const FPropertyParams* Params)
            :Super(InOwner, Params)
        {}

        virtual FString ToString(const void* Data) const;
        
        virtual void SetIntPropertyValue(void* Data, uint64 Value) const override;
        virtual void SetIntPropertyValue(void* Data, int64 Value) const override;
        
        virtual int64 GetSignedIntPropertyValue(void const* Data) const override;
        virtual int64 GetSignedIntPropertyValue_InContainer(void const* Container) const override;
        
        virtual uint64 GetUnsignedIntPropertyValue(void const* Data) const override;
        virtual uint64 GetUnsignedIntPropertyValue_InContainer(void const* Container) const override;

        
    };
    
    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    FString TProperty_Numeric<TCPPType>::ToString(const void* Data) const
    {
        return eastl::to_string(TTypeInfo::GetPropertyValue(Data));
    }

    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    void TProperty_Numeric<TCPPType>::SetIntPropertyValue(void* Data, uint64 Value) const
    {
        TTypeInfo::SetPropertyValue(Data, static_cast<TCPPType>(Value)); 
    }

    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    void TProperty_Numeric<TCPPType>::SetIntPropertyValue(void* Data, int64 Value) const
    {
        TTypeInfo::SetPropertyValue(Data, static_cast<TCPPType>(Value)); 
    }

    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    int64 TProperty_Numeric<TCPPType>::GetSignedIntPropertyValue(void const* Data) const
    {
        return static_cast<int64>(TTypeInfo::GetPropertyValue(Data));
    }

    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    int64 TProperty_Numeric<TCPPType>::GetSignedIntPropertyValue_InContainer(void const* Container) const
    {
        return static_cast<int64>(TTypeInfo::GetPropertyValue(Container));
    }

    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    uint64 TProperty_Numeric<TCPPType>::GetUnsignedIntPropertyValue(void const* Data) const
    {
        return static_cast<uint64>(TTypeInfo::GetPropertyValue(Data));
    }

    template <typename TCPPType> requires eastl::is_arithmetic_v<TCPPType>
    uint64 TProperty_Numeric<TCPPType>::GetUnsignedIntPropertyValue_InContainer(void const* Container) const
    {
        return static_cast<uint64>(TTypeInfo::GetPropertyValue(Container));
    }
    
    //-------------------------------------------------------------------------------
    
    class FBoolProperty : public TProperty_Numeric<bool>
    {
    public:
        using Super = TProperty_Numeric<bool>;

        DECLARE_FPROPERTY(EPropertyTypeFlags::Bool)
        
        FBoolProperty(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };
    
    class FInt8Property : public TProperty_Numeric<int8>
    {
    public:
        using Super = TProperty_Numeric<int8>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::Int8)

        FInt8Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FInt16Property : public TProperty_Numeric<int16>
    {
    public:
        using Super = TProperty_Numeric<int16>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::Int16)

        FInt16Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FInt32Property : public TProperty_Numeric<int32>
    {
    public:
        using Super = TProperty_Numeric<int32>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::Int32)

        FInt32Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FInt64Property : public TProperty_Numeric<int64>
    {
    public:
        using Super = TProperty_Numeric<int64>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::Int64)

        FInt64Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FUInt8Property : public TProperty_Numeric<uint8>
    {
    public:
        using Super = TProperty_Numeric<uint8>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::UInt8)

        FUInt8Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FUInt16Property : public TProperty_Numeric<uint16>
    {
    public:
        using Super = TProperty_Numeric<uint16>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::UInt16)

        FUInt16Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FUInt32Property : public TProperty_Numeric<uint32>
    {
    public:
        using Super = TProperty_Numeric<uint32>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::UInt32)

        FUInt32Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FUInt64Property : public TProperty_Numeric<uint64>
    {
    public:
        using Super = TProperty_Numeric<uint64>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::UInt64)

        FUInt64Property(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FFloatProperty : public TProperty_Numeric<float>
    {
    public:
        using Super = TProperty_Numeric<float>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::Float)

        FFloatProperty(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    class FDoubleProperty : public TProperty_Numeric<double>
    {
    public:
        using Super = TProperty_Numeric<double>;
        DECLARE_FPROPERTY(EPropertyTypeFlags::Double)

        FDoubleProperty(const FFieldOwner& InOwner, const FPropertyParams* Params)
            : Super(InOwner, Params)
        {}
    };

    

}
