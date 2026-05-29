#include "pch.h"
#include "Core/Object/Class.h"
#include "Core/Object/Field.h"
#include "Containers/Function.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Properties/ArrayProperty.h"
#include "Core/Reflection/Type/Properties/PropertyTag.h"

IMPLEMENT_INTRINSIC_CLASS(CStruct, CField, RUNTIME_API)

namespace Lumina
{

    void CStruct::SetSuperStruct(CStruct* InSuper)
    {
        SuperStruct = InSuper;
    }

    void CStruct::RegisterDependencies()
    {
        if (SuperStruct != nullptr)
        {
            SuperStruct->RegisterDependencies();
        }
    }

    bool CStruct::IsChildOf(const CStruct* Base) const
    {
        // UB to call on nullptr.
        ASSUME(this);
        
        if (Base == nullptr)
        {
            return false;
        }

        bool bOldResult = false;
        for (const CStruct* Temp = this; Temp; Temp = Temp->GetSuperStruct())
        {
            if (Temp == Base)
            {
                bOldResult = true;
                break;
            }
        }

        return bOldResult;
    }

    void CStruct::Link()
    {

        if (bLinked)
        {
            return;
        }
        bLinked = true;

        if (SuperStruct)
        { 
            SuperStruct->Link();
        }

        if (SuperStruct && SuperStruct->LinkedProperty)
        {
            FProperty* SuperProperty = SuperStruct->LinkedProperty;

            if (LinkedProperty == nullptr)
            {
                LinkedProperty = SuperProperty;
            }
            else
            {
                FProperty* Current = LinkedProperty;
                while (Current->Next != nullptr)
                {
                    Current = (FProperty*)Current->Next;
                }
                Current->Next = SuperProperty;
            }
        }
    }

    FFixedString CStruct::MakeDisplayName() const
    {
        FFixedString DisplayName = GetName().c_str();
        DisplayName.erase(0, 1);
        return DisplayName;
    }
    
    FProperty* CStruct::GetProperty(const FName& Name) const
    {
        for (FProperty* Current = LinkedProperty; Current; Current = (FProperty*)Current->Next)
        {
            if (Current->Name == Name)
            {
                return Current;
            }
        }
        
        return nullptr;
    }

    void CStruct::AddProperty(FProperty* Property)
    {
        if (LinkedProperty == nullptr)
        {
            LinkedProperty = Property;
        }
        else
        {
            FProperty* Current = LinkedProperty;
            while (Current->Next != nullptr)
            {
                Current = (FProperty*)Current->Next;
            }
            Current->Next = Property;
        }
        
        Property->Next = nullptr;
    }
    
    static bool ReadNumericValue(FArchive& Ar, const FName& TypeName, double& OutValue)
    {
        if (TypeName == "Int8Property") { int8 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "Int16Property") { int16 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "Int32Property") { int32 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "Int64Property") { int64 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "UInt8Property") { uint8 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "UInt16Property") { uint16 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "UInt32Property") { uint32 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "UInt64Property") { uint64 v; Ar << v; OutValue = v; return true; }
        if (TypeName == "FloatProperty") { float v; Ar << v; OutValue = v; return true; }
        if (TypeName == "DoubleProperty") { Ar << OutValue; return true; }
        return false;
    }
    
    void CStruct::SerializeTaggedProperties(FArchive& Ar, void* Data) const
    {
        if (StructOps && StructOps->HasSerializer())
        {
            StructOps->Serialize(Ar, Data);
            return;
        }
        
        if (Ar.IsWriting())
        {
            uint32 NumProperties = 0;
            int64 NumPropertiesWritePos = Ar.Tell();
            Ar << NumProperties;
            
            for (FProperty* Current = LinkedProperty; Current; Current = (FProperty*)Current->Next)
            {
                if (!Current->ShouldSerialize())
                {
                    continue;
                }
                // Cook-mode strip: editor-only properties are absent in
                // shipped packages.
                if (Ar.IsCooking() && Current->IsEditorOnly())
                {
                    continue;
                }

                FPropertyTag PropertyTag;
                PropertyTag.Type = Current->GetTypeName();
                PropertyTag.Name = Current->GetPropertyName();

                // Placeholder tag; rewritten with final size after serialize.
                int64 TagPosition = Ar.Tell();
                Ar << PropertyTag;
                int64 AfterTagPosition = Ar.Tell();
            
                PropertyTag.Offset = AfterTagPosition;
                
                void* ValuePtr = Current->GetValuePtr<void>(Data);

                Current->Serialize(Ar, ValuePtr);

                int64 DataEndPosition = Ar.Tell();
                PropertyTag.Size = (int32)(DataEndPosition - AfterTagPosition);

                Ar.Seek(TagPosition);
                Ar << PropertyTag;
            
                Ar.Seek(DataEndPosition);

                NumProperties++;
            }

            int64 Pos = Ar.Tell();
            Ar.Seek(NumPropertiesWritePos);
            Ar << NumProperties;
            Ar.Seek(Pos);

        }
        else if (Ar.IsReading())
        {
            uint32 NumProperties = 0;
            Ar << NumProperties;

            if (NumProperties > Ar.GetMaxSerializeSize())
            {
                LOG_ERROR("Archiver corrupted: struct claims {} tagged properties (max {})", NumProperties, Ar.GetMaxSerializeSize());
                Ar.SetHasError(true);
                return;
            }

            FProperty* Current = LinkedProperty;
            for (uint32 i = 0; i < NumProperties; ++i)
            {
                FPropertyTag Tag;
                Ar << Tag;
        
                int64 DataStartPos = Ar.Tell();
        
                FProperty* FoundProperty = nullptr;

                // O(n) fast path assuming order is unchanged.
                if (Current && Current->GetPropertyName() == Tag.Name)
                {
                    FoundProperty = Current;
                    Current = (FProperty*)Current->Next;
                }

                // O(n^2) fallback for reordered properties.
                if (FoundProperty == nullptr)
                {
                    for (FProperty* Search = LinkedProperty; Search; Search = (FProperty*)Search->Next)
                    {
                        if (Search->GetPropertyName() == Tag.Name)
                        {
                            FoundProperty = Search;
                            break;
                        }
                    }
                }
        
                if (FoundProperty)
                {
                    if (!FoundProperty->ShouldSerialize())
                    {
                        LOG_WARN("Property '{}' that was previously serialized, is not marked transient. Skipping.", Tag.Name.ToString());
                        Ar.Seek(DataStartPos + Tag.Size);
                        continue;
                    }
                    
                    if (FoundProperty->GetTypeName() == Tag.Type)
                    {
                        void* ValuePtr = FoundProperty->GetValuePtr<void>(Data);
                        FoundProperty->Serialize(Ar, ValuePtr);
                    }
                    else if (IsPropertyNumeric(FoundProperty->GetTypeName()) && IsPropertyNumeric(Tag.Type))
                    {
                        double OldValue = 0.0;
                        if (!ReadNumericValue(Ar, Tag.Type, OldValue))
                        {
                            LOG_ERROR("Failed to read numeric value for property '{}'", Tag.Name);
                        }
                        else if (IsValueValidForType(OldValue, FoundProperty->GetTypeName()))
                        {
                            FoundProperty->SetValue(Data, OldValue);
                                            
                            LOG_WARN("Property '{}' type changed from '{}' to '{}', converted value to new type.", 
                            Tag.Name, Tag.Type, FoundProperty->GetTypeName());
                        }
                        else
                        {
                            LOG_WARN("Property '{}' type changed from '{}' to '{}', but the value cannot fit in the new type.", 
                            Tag.Name, Tag.Type, FoundProperty->GetTypeName());
                        }
                    }
                }
                else
                {
                    LOG_WARN("Property '{}' of type '{}' not found in struct, skipping", Tag.Name.ToString(), Tag.Type.ToString());
                }
        
                Ar.Seek(DataStartPos + Tag.Size);
            }
        }
    }
}
