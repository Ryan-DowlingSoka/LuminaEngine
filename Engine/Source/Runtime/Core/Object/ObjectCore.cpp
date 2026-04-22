#include "pch.h"

#include "ObjectCore.h"
#include "Class.h"
#include "Object.h"
#include "ObjectAllocator.h"
#include "ObjectHash.h"
#include "ObjectIterator.h"
#include "Assets/AssetManager/AssetManager.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Core/Engine/Engine.h"
#include "Core/Math/Math.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Properties/ArrayProperty.h"
#include "Core/Reflection/Type/Properties/EnumProperty.h"
#include "Core/Reflection/Type/Properties/ObjectProperty.h"
#include "Core/Reflection/Type/Properties/StringProperty.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "Package/Package.h"
#include "Paths/Paths.h"
#include "Scripting/Lua/Scripting.h"
#include "TaskSystem/ThreadedCallback.h"

namespace Lumina
{
    /** Allocates a section of memory for the new object, does not place anything into the newly allocated memory*/
    static void* AllocateCObjectMemory(const CClass* InClass, EObjectFlags InFlags)
    {
        // Force 16-byte minimal
        uint32 Alignment = Math::Max<uint32>(16, InClass->GetAlignment());
        
        return GCObjectAllocator.AllocateCObject(InClass->GetSize(), Alignment);
    }

    CObject* StaticAllocateObject(const FConstructCObjectParams& Params)
    {
        LUMINA_PROFILE_SCOPE();
        
        void* ObjectMemory = AllocateCObjectMemory(Params.Class, Params.Flags);
        Memory::Memzero(ObjectMemory, Params.Class->GetSize());
        
        CObject* NewObject = Params.Class->EmplaceInstance(ObjectMemory);

        NewObject->ConstructInternal(FObjectInitializer(Params.Package, Params));
        
        NewObject->PostInitProperties();
        
        return NewObject;
    }

    CObject* FindObjectImpl(const FGuid& ObjectGUID)
    {
        return static_cast<CObject*>(FObjectHashTables::Get().FindObject(ObjectGUID));
    }

    CObject* FindObjectImpl(const FName& Name, CClass* Class)
    {
        return static_cast<CObject*>(FObjectHashTables::Get().FindObject(Name, Class));
    }

    CObject* StaticLoadObject(const FGuid& GUID)
    {
        LUMINA_PROFILE_SCOPE();
        
        if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByGUID(GUID))
        {
            if (CPackage* Package = CPackage::LoadPackage(Data->Path))
            {
                return Package->LoadObject(GUID);
            }
        }
        
        return nullptr;
    }

    void AsyncLoadObject(const FGuid& GUID, const TFunction<void(CObject*)>& Callback)
    {
        Task::AsyncTask(1, 1, [GUID, Callback](uint32, uint32, uint32)
        {
            if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByGUID(GUID))
            {
                if (CPackage* Package = CPackage::LoadPackage(Data->Path))
                {
                    if (CObject* Object = Package->LoadObject(GUID))
                    {
                        MainThread::Enqueue([Object, Callback]
                        {
                            Callback(Object);
                        });
                    }
                }
            }
        });
    }

    void AsyncLoadObject(const FName& Name, const TFunction<void(CObject*)>& Callback)
    {
        Task::AsyncTask(1, 1, [Name, Callback](uint32, uint32, uint32)
        {
            if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(Name.c_str()))
            {
                if (CPackage* Package = CPackage::LoadPackage(Data->Path))
                {
                    if (CObject* Object = Package->LoadObject(Data->AssetGUID))
                    {
                        MainThread::Enqueue([Object, Callback]
                        {
                            Callback(Object);
                        });
                    }
                }
            }
        });
    }

    CObject* StaticLoadObject(FStringView Name)
    {
        if (const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(Name))
        {
            return StaticLoadObject(Data->AssetGUID);
        }
        
        return nullptr;
    }

    FFixedString SanitizeObjectName(FStringView Name)
    {
        // Strip extension
        const size_t ExtPos = Name.find_last_of('.');
        if (ExtPos != FString::npos)
        {
            Name = Name.substr(0, ExtPos);
        }

        FFixedString Result;
        Result.reserve(Name.size());

        for (char c : Name)
        {
            const bool bInvalid =
                c < 32 ||
                c == '<' || c == '>' ||
                c == ':' || c == '"' ||
                c == '|' || c == '?' ||
                c == '*';

            Result.push_back(bInvalid ? '_' : c);
        }

        while (!Result.empty() && (Result.back() == ' ' || Result.back() == '.'))
        {
            Result.pop_back();
        }

        if (Result.empty())
        {
            Result = "Object";
        }

        return Result;
    }

    bool IsValid(const CObjectBase* Obj)
    {
        if (Obj == nullptr)
        {
            return false;
        }

        if (Obj->HasAnyFlag(OF_NeedsLoad))
        {
            return false;
        }

        return true;
    }

    bool IsValid(CObjectBase* Obj)
    {
        if (Obj == nullptr)
        {
            return false;
        }

        if (Obj->HasAnyFlag(OF_NeedsLoad))
        {
            return false;
        }

        return true;
    }
    
    CObject* NewObject(CClass* InClass, CPackage* Package, const FName& Name, const FGuid& GUID, EObjectFlags Flags)
    {
        FConstructCObjectParams Params(InClass);

        if (Name == NAME_None)
        {
            Params.Name = InClass->GetName() + "_" + eastl::to_string(++InClass->ClassUnique);
        }
        else
        {
            Params.Name = Name;
        }

        
        Params.Guid = GUID;
        Params.Flags = Flags;
        Params.Package = Package;

        return StaticAllocateObject(Params);
    }
    
    void GetObjectsWithPackage(const CPackage* Package, TVector<CObject*>& OutObjects)
    {
        ASSERT(Package != nullptr);
        
        for (TObjectIterator<CObject> It; It; ++It)
        {
            CObjectBase* Object = *It;
            if (Object && Object->GetPackage() == Package)
            {
                OutObjects.push_back(static_cast<CObject*>(Object));
            }
        }
    }

    bool IsValueValidForType(double Value, const FName& TypeName)
    {
        if (TypeName == "Int8Property")
        {
            return Value >= INT8_MIN && Value <= INT8_MAX;
        }
        if (TypeName == "Int16Property")
        {
            return Value >= INT16_MIN && Value <= INT16_MAX;
        }
        if (TypeName == "IntProperty")
        {
            return Value >= INT32_MIN && Value <= INT32_MAX;
        }
        if (TypeName == "Int64Property")
        {
            return Value >= (double)INT64_MIN && Value <= (double)INT64_MAX;
        }
        if (TypeName == "UInt8Property")
        {
            return Value >= 0 && Value <= UINT8_MAX;
        }
        if (TypeName == "UInt16Property")
        {
            return Value >= 0 && Value <= UINT16_MAX;
        }
        if (TypeName == "UInt32Property")
        {
            return Value >= 0 && Value <= UINT32_MAX;
        }
        if (TypeName == "UInt64Property")
        {
            return Value >= 0 && Value <= (double)UINT64_MAX;
        }
        if (TypeName == "FloatProperty" || TypeName == "DoubleProperty")
        {
            return true;
        }
        return false;
    }

    bool IsPropertyNumeric(const FName& Type)
    {
        return Type == "Int8Property" ||
            Type == "Int16Property" ||
            Type == "Int32Property" ||
            Type == "Int64Property" ||
            Type == "UInt8Property" ||
            Type == "UInt16Property" ||
            Type == "UInt32Property" ||
            Type == "UInt64Property" ||
            Type == "FloatProperty" || 
            Type == "DoubleProperty";
    }

    static void ConstructPropertyMetadata(FProperty* NewProperty, uint16 NumMetadata, const FMetaDataPairParam* ParamArray)
    {
        for (uint16 i = 0; i < NumMetadata; ++i)
        {
            const FMetaDataPairParam& Param = ParamArray[i];
            NewProperty->Metadata.AddValue(Param.NameUTF8, Param.ValueUTF8);
        }
        
        NewProperty->OnMetadataFinalized();
    }

    template<typename TPropertyType, typename TPropertyParamType>
    TPropertyType* NewFProperty(FFieldOwner Owner, const FPropertyParams* Param)
    {
        const TPropertyParamType* TypedParam = static_cast<const TPropertyParamType*>(Param);
        TPropertyType* Type = nullptr;
        
        if(Param->GetterFunc || Param->SetterFunc)
        {
            Type = Memory::New<TPropertyWithSetterAndGetter<TPropertyType>>(Owner, TypedParam);
        }
        else
        {
            Type = Memory::New<TPropertyType>(Owner, TypedParam);
        }
        
        if (TypedParam->NumMetaData)
        {
            ConstructPropertyMetadata(Type, TypedParam->NumMetaData, TypedParam->MetaDataArray);
        }
        return Type;
    }
    
    
    void ConstructProperties(const FFieldOwner& FieldOwner, const FPropertyParams* const*& Properties, uint32& NumProperties)
    {
        const FPropertyParams* Param = *--Properties;

        // Indicates the property has an associated inner property, which would be next in the Properties list.
        uint32 ReadMore = 0;

        
        FProperty* NewProperty = nullptr;
    
        switch (Param->TypeFlags)
        {
        case EPropertyTypeFlags::Int8:
            NewFProperty<FInt8Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Int16:
            NewFProperty<FInt16Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Int32:
            NewFProperty<FInt32Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Int64:
            NewFProperty<FInt64Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::UInt8:
            NewFProperty<FUInt8Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::UInt16:
            NewFProperty<FUInt16Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::UInt32:
            NewFProperty<FUInt32Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::UInt64:
            NewFProperty<FUInt64Property, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Float:
            NewFProperty<FFloatProperty, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Double:
            NewFProperty<FDoubleProperty, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Bool:
            NewFProperty<FBoolProperty, FNumericPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Object:
            NewFProperty<FObjectProperty, FObjectPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Class:
            break;
        case EPropertyTypeFlags::Name:
            NewFProperty<FNameProperty, FNamePropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::String:
            NewFProperty<FStringProperty, FStringPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Struct:
            NewFProperty<FStructProperty, FStructPropertyParams>(FieldOwner, Param);
            break;
        case EPropertyTypeFlags::Enum:
            {
                NewProperty = NewFProperty<FEnumProperty, FEnumPropertyParams>(FieldOwner, Param);
                
                ReadMore = 1;
            }
            break;
        case EPropertyTypeFlags::Vector:
            {
                NewProperty = NewFProperty<FArrayProperty, FArrayPropertyParams>(FieldOwner, Param);

                ReadMore = 1;
            }
            break;
        default:
            {
                LOG_CRITICAL("Unsupported property type found while creating: {}", Param->Name);
            }
            break;
        }

        --NumProperties;
        for (; ReadMore; --ReadMore)
        {
            FFieldOwner Owner;
            Owner.emplace<FField*>(NewProperty);
            ConstructProperties(Owner, Properties, NumProperties);
        }
    }

    void InitializeAndCreateFProperties(CStruct* Outer, const FPropertyParams* const* PropertyArray, uint32 NumProperties)
    {
        // Move to iterate backwards.
        PropertyArray += NumProperties;
        while (NumProperties)
        {
            FFieldOwner Owner;
            Owner.emplace<CStruct*>(Outer);
            ConstructProperties(Owner, PropertyArray, NumProperties);
        }
    }

    static CPackage* FindOrCreatePackage(const TCHAR* PackageName)
    {
        CPackage* Package = nullptr;
        if (PackageName && PackageName[0] != '\0')
        {
            Package = FindObject<CPackage>(PackageName);
            if (Package == nullptr)
            {
                Package = NewObject<CPackage>(nullptr, PackageName);
            }
        }

        return Package;
    }
    
    void ConstructCClass(CClass** OutClass, const FClassParams& Params)
    {
        CClass*& FinalClass = *OutClass;
        if (FinalClass != nullptr)
        {
            return;
        }
        
        FinalClass = Params.RegisterFunc();

        CObjectForceRegistration(FinalClass);
        
        InitializeAndCreateFProperties(FinalClass, Params.Params, Params.NumProperties);
        
        for (uint16 i = 0; i < Params.NumMetaData; ++i)
        {
            const FMetaDataPairParam& Param = Params.MetaDataArray[i];
            FinalClass->Metadata.AddValue(Param.NameUTF8, Param.ValueUTF8);
        }
        
        lua_State* LuaVM = Lua::FScriptingContext::Get().GetVM();
        Params.LuaRegisterFn(LuaVM);
        
        // Link this class to its parent. (if it has one).
        //FinalClass->Link();
    }

    void ConstructCEnum(CEnum** OutEnum, const FEnumParams& Params)
    {
        FConstructCObjectParams ObjectParms(CEnum::StaticClass());
        ObjectParms.Name        = Params.Name;
        ObjectParms.Flags       = OF_None;
        ObjectParms.Package     = FindOrCreatePackage(CEnum::StaticPackage());
        ObjectParms.Guid        = FGuid::New();

        
        CEnum* NewEnum = (CEnum*)StaticAllocateObject(ObjectParms);
        
        *OutEnum = NewEnum;
        
        lua_State* LuaVM = Lua::FScriptingContext::Get().GetVM();
        Params.LuaRegisterFn(LuaVM);
        
        for (uint16 i = 0; i < Params.NumMetaData; ++i)
        {
            const FMetaDataPairParam& Param = Params.MetaDataArray[i];
            NewEnum->Metadata.AddValue(Param.NameUTF8, Param.ValueUTF8);
        }
        
        for (int16 i = 0; i < Params.NumParams; i++)
        {
            const FEnumeratorParam* Param = &Params.Params[i];
            NewEnum->AddEnum(Param->NameUTF8, Param->Value);
        }

        NewEnum->AddToRoot();
    }
    
    void ConstructCStruct(CStruct** OutStruct, const FStructParams& Params)
    {
        FConstructCObjectParams ObjectParms(CStruct::StaticClass());
        ObjectParms.Name        = Params.Name;
        ObjectParms.Flags       = OF_None;
        ObjectParms.Package     = FindOrCreatePackage(CStruct::StaticPackage());
        ObjectParms.Guid        = FGuid::New();
        
        CStruct* FinalClass = (CStruct*)StaticAllocateObject(ObjectParms);
        FinalClass->Size = Params.SizeOf;
        FinalClass->Alignment = Params.AlignOf;
        FinalClass->StructOps.reset(Params.StructOpsFn());
        
        
        lua_State* LuaVM = Lua::FScriptingContext::Get().GetVM();
        Params.LuaRegisterFn(LuaVM);
        
        *OutStruct = FinalClass;
        
        CObjectForceRegistration(FinalClass);
        
        InitializeAndCreateFProperties(FinalClass, Params.Params, Params.NumProperties);
        
        for (uint16 i = 0; i < Params.NumMetaData; ++i)
        {
            const FMetaDataPairParam& Param = Params.MetaDataArray[i];
            FinalClass->Metadata.AddValue(Param.NameUTF8, Param.ValueUTF8);
        }

        if (Params.SuperFunc)
        {
            CStruct* SuperStruct = Params.SuperFunc();
            FinalClass->SetSuperStruct(SuperStruct);
        }
        
        FinalClass->Link();

        FinalClass->AddToRoot();
    }
    
}
