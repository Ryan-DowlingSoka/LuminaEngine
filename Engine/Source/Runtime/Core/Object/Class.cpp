#include "pch.h"
#include "Class.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Metadata/PropertyMetadata.h"
#include "Package/Package.h"

namespace Lumina
{
    RUNTIME_API void AllocateStaticClass(const TCHAR* Package, const TCHAR* Name, CClass** OutClass, uint32 Size, uint32 Alignment, CClass* (*SuperClassFn)(), CClass::FactoryFunctionType FactoryFunc)
    {
        DEBUG_ASSERT(*OutClass == nullptr);
        
        CPackage* PackageObject = nullptr;

        if (Package && Package[0] != '\0')
        {
            PackageObject = FindObject<CPackage>(Package);
            if (PackageObject == nullptr)
            {
                PackageObject = NewObject<CPackage>(nullptr, Package);
            }
        }

        *OutClass = Memory::New<CClass>(PackageObject, FName(Name), Size, Alignment, OF_None, FactoryFunc);
        
        CClass* NewClass = *OutClass;
        CClass* SuperClass = SuperClassFn();
        bool bValidSuperClass = (SuperClass != NewClass);
        
        NewClass->SetSuperStruct(bValidSuperClass ? SuperClass : nullptr);

        NewClass->RegisterDependencies();
        NewClass->BeginRegister();
    }
    

    //-----------------------------------------------------------------------------------------------------------------------------------------

    
    IMPLEMENT_INTRINSIC_CLASS(CClass, CStruct, RUNTIME_API)

    //-----------------------------------------------------------------------------------------------

    bool CField::HasMeta(const FName& Key) const
    {
        return Metadata.HasMetadata(Key);
    }

    const FString& CField::GetMeta(const FName& Key) const
    {
        return Metadata.GetMetadata(Key);
    }

    CObject* CClass::EmplaceInstance(void* Memory) const
    {
        DEBUG_ASSERT(FactoryFunction);
        return FactoryFunction(Memory);
    }

    CClass* CClass::GetSuperClass() const
    {
        return static_cast<CClass*>(GetSuperStruct());
    }

    CObject* CClass::GetDefaultObject() const
    {
        if (ClassDefaultObject == nullptr)
        {
            CClass* MutableThis = const_cast<CClass*>(this);
            MutableThis->CreateDefaultObject();
        }

        return ClassDefaultObject;
    }

    void* CStruct::GetDefaultInstance()
    {
        if (DefaultInstance != nullptr)
        {
            return DefaultInstance;
        }

        FStructOps* Ops = GetStructOps();
        if (Ops == nullptr || !Ops->HasConstruct())
        {
            return nullptr;
        }

        // Aligned raw allocation, default-construct in place. Lifetime is the
        // process — we deliberately don't track these for cleanup, mirroring
        // how class CDOs are AddToRoot'd and never released.
        const uint32 InstanceSize = GetSize();
        const uint32 InstanceAlign = GetAlignment();
        DefaultInstance = Memory::Malloc(InstanceSize, InstanceAlign);
        Ops->Construct(DefaultInstance);
        return DefaultInstance;
    }

    void* CClass::GetDefaultInstance()
    {
        return GetDefaultObject();
    }

    CObject* CClass::CreateDefaultObject()
    {
        DEBUG_ASSERT(ClassDefaultObject == nullptr);
        
        Link();
        
        FString DefaultObjectName = GetName().c_str();
        DefaultObjectName += "_CDO";
        
        FConstructCObjectParams Params(this);
        Params.Flags    |= OF_DefaultObject;
        Params.Name     = FName(DefaultObjectName);
        Params.Package  = GetPackage();
        Params.Guid     = FGuid::New();
        
        ClassDefaultObject = StaticAllocateObject(Params);
        ClassDefaultObject->AddToRoot();
        
        ClassDefaultObject->SetFlag(OF_DefaultObject);

        ClassDefaultObject->PostCreateCDO();
        
        return ClassDefaultObject;
    }
    
    static CStruct* StaticGetBaseStructureInternal(const FName& Name)
    {
        CStruct* Result = static_cast<CStruct*>(FindObjectImpl(Name, CStruct::StaticClass()));
        return Result;
    }

    CStruct* TBaseStructure<glm::vec<2, float>>::Get()
    {
	    static CStruct* Struct = StaticGetBaseStructureInternal("vec2");
        return Struct;
    }

    CStruct* TBaseStructure<glm::vec<3, float>>::Get()
    {
        static CStruct* Struct = StaticGetBaseStructureInternal("vec3");
        return Struct;
    }

    CStruct* TBaseStructure<glm::vec<4, float>>::Get()
    {
        static CStruct* Struct = StaticGetBaseStructureInternal("vec4");
        return Struct;   
    }

    CStruct* TBaseStructure<glm::quat>::Get()
    {
        static CStruct* Struct = StaticGetBaseStructureInternal("quat");
        return Struct;   
    }
}
