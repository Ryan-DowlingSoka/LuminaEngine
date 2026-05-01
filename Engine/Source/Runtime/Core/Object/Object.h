#pragma once

#include "ObjectBase.h"
#include "ObjectMacros.h"
#include "Core/Serialization/Archiver.h"
#include "Core/Serialization/Structured/StructuredArchive.h"


namespace Lumina
{
    class FProperty;
    class IStructuredArchive;
    class CClass;
    class CObject;
}

RUNTIME_API Lumina::CClass* Construct_CClass_Lumina_CObject();

namespace Lumina
{

    // Base object for all reflected Lumina types. Path format: <package-path>.<object-name>
    class CObject : public CObjectBase
    {
    public:

        friend CObject* StaticAllocateObject();

        DECLARE_CLASS(Lumina, CObject, CObject, "/Script/Engine", RUNTIME_API)
        DEFINE_CLASS_FACTORY(CObject)

        CObject() = default;
        
        /** Internal constructor */
        RUNTIME_API CObject(EObjectFlags InFlags)
            : CObjectBase(InFlags)
        {}

        /** Internal constructor */
        RUNTIME_API CObject(CClass* InClass, EObjectFlags InFlags, CPackage* Package, const FName& InName, const FGuid& GUID)
            :CObjectBase(InClass, InFlags, Package, InName, GUID)
        {}
        
        /** Serializes object data. Can be overridden by derived classes. */
        RUNTIME_API virtual void Serialize(FArchive& Ar);

        /** Used during serialization to and from a structured archive (Packaging, Network, etc.). */
        RUNTIME_API virtual void Serialize(IStructuredArchive::FRecord Record);
        
        /** Called after constructor and after properties have been initialized. */
        RUNTIME_API virtual void PostInitProperties();

        /** Called after classes Class Default Object has been created */
        RUNTIME_API virtual void PostCreateCDO() {}

        /** Is the object considered an asset? */
        RUNTIME_API virtual bool IsAsset() const { return false; }
        
        /** If this object is an asset, should it serialize as binary, or a text format */
        RUNTIME_API virtual bool IsBinary() const { return true; }
        
        /** Called just before the object is serialized from disk */
        RUNTIME_API virtual void PreLoad() {}
        
        /** Called immediately after the object is serialized from disk */
        RUNTIME_API virtual void PostLoad() {}

        /** Called when a property on this object has been modified externally */
        RUNTIME_API virtual void PostPropertyChange(FProperty* ChangedProperty) {}

        /** Renames this object, optionally changing its package */
        RUNTIME_API virtual bool Rename(const FName& NewName, CPackage* NewPackage = nullptr);
        
        /** Uses this object as a template to construct a new object from, will only copy reflected properties. */
        RUNTIME_API CObject* Duplicate();
        
        /** Copies this object's properties over to another property of the same class */
        RUNTIME_API void CopyPropertiesTo(CObject* Other);
        
    private:

    };
}
