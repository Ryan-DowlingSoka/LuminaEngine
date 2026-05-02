#pragma once

#include "ConstructObjectParams.h"
#include "ObjectFlags.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Core/LuminaMacros.h"
#include "Platform/GenericPlatform.h"

// Forward-declare to avoid pulling in <lua.h>; only appears as pointer in typedefs below.
extern "C" { struct lua_State; }


namespace Lumina
{
    struct FStructOps;
    class CPackage;
    class CStruct;
    class CObjectBase;
    class CEnum;
    class CObject;
    class CClass;
}

namespace Lumina
{

    namespace Concept
    {
        template<typename T>
        concept IsACObject = eastl::is_base_of_v<CObject, T>;
    }
    
    RUNTIME_API CObject* StaticAllocateObject(const FConstructCObjectParams& Params);

    RUNTIME_API CObject* FindObjectImpl(const FGuid& ObjectGUID);
    RUNTIME_API CObject* FindObjectImpl(const FName& Name, CClass* Class);
    RUNTIME_API CObject* StaticLoadObject(const FGuid& GUID);
    RUNTIME_API void AsyncLoadObject(const FGuid& GUID, const TFunction<void(CObject*)>& Callback);
    RUNTIME_API void AsyncLoadObject(const FName& Name, const TFunction<void(CObject*)>& Callback);
    RUNTIME_API CObject* StaticLoadObject(FStringView Name);
    RUNTIME_API FFixedString SanitizeObjectName(FStringView Name);

    RUNTIME_API bool IsValid(const CObjectBase* Obj);
    RUNTIME_API bool IsValid(CObjectBase* Obj);
    
    template<Concept::IsACObject T>
    T* FindObject(const FGuid& GUID)
    {
        return static_cast<T*>(FindObjectImpl(GUID));
    }

    template<Concept::IsACObject T>
    T* FindObject(const FName& Name)
    {
        return static_cast<T*>(FindObjectImpl(Name, T::StaticClass()));
    }

    template<Concept::IsACObject T>
    T* FindObject(CClass& Class, const FName& Name)
    {
        return static_cast<T*>(FindObjectImpl(Name, &Class));
    }

    template<Concept::IsACObject T>
    T* LoadObject(const FGuid& GUID)
    {
        return static_cast<T*>(StaticLoadObject(GUID));
    }
    
    template<Concept::IsACObject T>
    T* LoadObject(FStringView Name)
    {
        return static_cast<T*>(StaticLoadObject(Name));
    }
    
    RUNTIME_API CObject* NewObject(CClass* InClass, CPackage* Package = nullptr, const FName& Name = NAME_None, const FGuid& GUID = FGuid::New(), EObjectFlags Flags = OF_None);
    RUNTIME_API void GetObjectsWithPackage(const CPackage* Package, TVector<CObject*>& OutObjects);


    template<Concept::IsACObject T>
    T* NewObject(EObjectFlags Flags)
    {
        return static_cast<T*>(NewObject(T::StaticClass(), nullptr, NAME_None, FGuid::New(), Flags));
    }
    
    template<Concept::IsACObject T>
    T* NewObject(CPackage* Package = nullptr, const FName& Name = NAME_None, const FGuid& GUID = FGuid::New(), EObjectFlags Flags = OF_None)
    {
        return static_cast<T*>(NewObject(T::StaticClass(), Package, Name, GUID, Flags));
    }

    template<Concept::IsACObject T>
    T* NewObject(CClass* InClass, CPackage* Package = nullptr, const FName& Name = NAME_None, const FGuid& GUID = FGuid::New(), EObjectFlags Flags = OF_None)
    {
        return static_cast<T*>(NewObject(InClass, Package, Name, GUID, Flags));
    }

    template<Concept::IsACObject T>
    T* GetMutableDefault()
    {
        return T::StaticClass()->template GetDefaultObject<T>();
    }

    template<Concept::IsACObject T>
    const T* GetDefault()
    {
        return T::StaticClass()->template GetDefaultObject<T>();
    }
    
    enum class EPropertyFlags : uint16
    {
        None                = 0,
        Editable            = BIT(0),
        ReadOnly            = BIT(1),
        NoSerialize         = BIT(2),
        Const               = BIT(3),
        Private             = BIT(4),
        Protected           = BIT(5),
        SubField            = BIT(6),
        Trivial             = BIT(7),
        Script              = BIT(8),
        Builtin             = BIT(9),
        BulkSerialize       = BIT(10),
    };

    ENUM_CLASS_FLAGS(EPropertyFlags);

    /** Must mirror EPropertyTypeFlags in ReflectedType.h. */
    enum class EPropertyTypeFlags : uint8
    {
        None = 0,

        Int8,
        Int16,
        Int32,
        Int64,

        UInt8,
        UInt16,
        UInt32,
        UInt64,

        Float,
        Double,

        Bool,
        Object,
        Class,
        Name,
        String,
        Enum,
        Vector,
        Struct,
        Optional,

        Count,
    };

    ENUM_CLASS_FLAGS(EPropertyTypeFlags);

    inline constexpr const char* PropertyTypeFlagNames[] =
    {
        "None",
        "Int8Property",
        "Int16Property",
        "Int32Property",
        "Int64Property",

        "UInt8Property",
        "UInt16Property",
        "UInt32Property",
        "UInt64Property",

        "FloatProperty",
        "DoubleProperty",

        "BoolProperty",
        "ObjectProperty",
        "ClassProperty",
        "NameProperty",
        "StringProperty",
        "EnumProperty",
        "VectorProperty",
        "StructProperty",
        "OptionalProperty"
    };

    static_assert(std::size(PropertyTypeFlagNames) == (size_t)EPropertyTypeFlags::Count, "PropertyTypeFlagStrings must match number of flags in EPropertyTypeFlags");
    
    inline const char* PropertyTypeToString(EPropertyTypeFlags Flag)
    {
        uint16 Index = static_cast<uint16>(Flag);
        return PropertyTypeFlagNames[Index];
    }
    
    RUNTIME_API EPropertyTypeFlags PropertyStringToType(FName String);
    
    RUNTIME_API bool IsValueValidForType(double Value, const FName& TypeName);
    RUNTIME_API bool IsPropertyNumeric(const FName& Type);
    
    template <typename T>
    struct TRegistrationInfo
    {
        using TType = T;
        
        /** Allocates the class memory in stage 1. */
        TType* InnerSingleton = nullptr;

        /** Stage 2: holds property/function reflection initialization. */
        TType* OuterSingleton = nullptr;

    };

    using FStructRegistrationInfo   = TRegistrationInfo<CStruct>;
    using FClassRegistrationInfo    = TRegistrationInfo<CClass>;
    using FEnumRegistrationInfo     = TRegistrationInfo<CEnum>;

    struct FMetaDataPairParam
    {
        const char* NameUTF8;
        const char* ValueUTF8;
    };

    typedef void (*SetterFuncPtr)(void* InContainer, const void* InValue);
    typedef void (*GetterFuncPtr)(const void* InContainer, void* OutValue);

    typedef void (*ArrayPushBackPtr)(void* InContainer, const void* InValue);
    typedef size_t (*ArrayGetNumPtr)(const void* InContainer);
    typedef void (*ArrayRemoveAtPtr)(void* InContainer, size_t Index);
    typedef void (*ArrayClearPtr)(void* InContainer);
    typedef void* (*ArrayGetAtPtr)(void* InContainer, size_t Index);
    typedef void (*ArrayResizePtr)(void* InContainer, size_t Index);
    typedef void (*ArrayReservePtr)(void* InContainer, size_t Index);
    typedef void (*ArraySwapPtr)(void* InContainer, size_t LHS, size_t RHS);

    typedef bool (*OptionalHasValuePtr)(const void* InContainer);
    /** Only valid when HasValue returned true. */
    typedef void* (*OptionalGetValuePtr)(void* InContainer);
    /** Engages the optional; copies InValue if non-null, else default-constructs. */
    typedef void (*OptionalSetValuePtr)(void* InContainer, const void* InValue);
    typedef void (*OptionalResetPtr)(void* InContainer);


    struct FPropertyParams
    {
        const char*         Name;
        EPropertyFlags      PropertyFlags;
        EPropertyTypeFlags  TypeFlags;
        SetterFuncPtr       SetterFunc;
        GetterFuncPtr       GetterFunc;
        uint16              Offset;
    };

    struct FNumericPropertyParams : FPropertyParams
    {
        uint16                      NumMetaData;
        const FMetaDataPairParam*   MetaDataArray;
    };

    struct FStringPropertyParams : FPropertyParams
    {
        uint16                      NumMetaData;
        const FMetaDataPairParam*   MetaDataArray;
    };

    struct FNamePropertyParams : FPropertyParams
    {
        uint16                      NumMetaData;
        const FMetaDataPairParam*   MetaDataArray;
    };

    struct FObjectPropertyParams : FPropertyParams
    {
        CClass*                     (*ClassFunc)();

        uint16                      NumMetaData;
        const FMetaDataPairParam*   MetaDataArray;
    };

    struct FClassPropertyParams : FPropertyParams
    {
        CClass*                     (*ClassFunc)();

        uint16                      NumMetaData;
        const FMetaDataPairParam*   MetaDataArray;
    };

    struct FStructPropertyParams : FPropertyParams
    {
        CStruct*            (*StructFunc)();

        uint16 NumMetaData;
        const FMetaDataPairParam* MetaDataArray;
    };

    struct FEnumPropertyParams : FPropertyParams
    {
        CEnum*              (*EnumFunc)();

        uint16 NumMetaData;
        const FMetaDataPairParam* MetaDataArray;
    };

    struct FArrayPropertyParams : FPropertyParams
    {
        ArrayPushBackPtr    PushBackFn;
        ArrayGetNumPtr      GetNumFn;
        ArrayRemoveAtPtr    RemoveAtFn;
        ArrayClearPtr       ClearFn;
        ArrayGetAtPtr       GetAtFn;
        ArrayResizePtr      ResizeFn;
        ArrayReservePtr     ReserveFn;
        ArraySwapPtr        SwapFn;

        uint16 NumMetaData;
        const FMetaDataPairParam* MetaDataArray;
    };

    struct FOptionalPropertyParams : FPropertyParams
    {
        OptionalHasValuePtr HasValueFn;
        OptionalGetValuePtr GetValueFn;
        OptionalSetValuePtr SetValueFn;
        OptionalResetPtr    ResetFn;

        uint16 NumMetaData;
        const FMetaDataPairParam* MetaDataArray;
    };
    
    struct FClassParams
    {
        CClass*                         (*RegisterFunc)();
        void                            (*LuaRegisterFn)(lua_State*);

        const FPropertyParams* const*   Params;
        uint32                          NumProperties;
        
        uint16                          NumMetaData;
        const FMetaDataPairParam*       MetaDataArray;
    };

    struct FStructParams
    {
        CStruct*                        (*SuperFunc)();
        FStructOps*                     (*StructOpsFn)();
        void                            (*LuaRegisterFn)(lua_State*);
        const char*                     Name;
        const FPropertyParams* const*   Params;
        uint32                          NumProperties;
        uint16                          SizeOf;
        uint16                          AlignOf;
        
        uint16 NumMetaData;
        const FMetaDataPairParam* MetaDataArray;
    };
    
    struct FEnumeratorParam
    {
        const char*               NameUTF8;
        int64                     Value;
    };
    
    struct FEnumParams
    {
        void                        (*LuaRegisterFn)(lua_State*);
        const char*                 Name;
        const FEnumeratorParam*     Params;
        int16                       NumParams;
        
        uint16                      NumMetaData;
        const FMetaDataPairParam*   MetaDataArray;
    };
    

    

    RUNTIME_API void ConstructCClass(CClass** OutClass, const FClassParams& Params);
    RUNTIME_API void ConstructCEnum(CEnum** OutEnum, const FEnumParams& Params);
    RUNTIME_API void ConstructCStruct(CStruct** OutStruct, const FStructParams& Params);
    
}
