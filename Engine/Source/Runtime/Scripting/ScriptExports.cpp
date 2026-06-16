#include "pch.h"
#include "ScriptExports.h"

#include "Core/Serialization/Archiver.h"
#include "Log/Log.h"
#include <cstdlib>

namespace Lumina::Scripting
{
    const FString* FScriptExportMeta::Find(const FName& Key) const
    {
        for (const FScriptExportMetaArg& Arg : Entries)
        {
            if (Arg.Key == Key)
            {
                return &Arg.Value;
            }
        }
        return nullptr;
    }

    void FScriptExportMeta::Set(const FName& Key, const FString& Value)
    {
        for (FScriptExportMetaArg& Arg : Entries)
        {
            if (Arg.Key == Key)
            {
                Arg.Value = Value;
                return;
            }
        }
        Entries.push_back(FScriptExportMetaArg{ Key, Value });
    }

    bool FScriptExportMeta::GetNumber(const FName& Key, double& OutValue) const
    {
        const FString* Value = Find(Key);
        if (!Value || Value->empty())
        {
            return false;
        }
        char* End = nullptr;
        const double Parsed = std::strtod(Value->c_str(), &End);
        if (End == Value->c_str())
        {
            return false;
        }
        OutValue = Parsed;
        return true;
    }

    bool FScriptPropertyValue::Serialize(FArchive& Ar)
    {
        // Format: [u8 Version][u32 PayloadSize][u8 KindRaw][body...]. PayloadSize backpatched on write,
        // used on read to skip unknown kinds/versions (reconcile refills with defaults).
        constexpr uint8 CurrentVersion = 2;
        uint8 Version = CurrentVersion;
        Ar << Version;

        if (Ar.IsReading() && Version != CurrentVersion)
        {
            LOG_WARN("FScriptPropertyValue - unknown version {}, resetting to default.", Version);
            Kind = EScriptExportKind::Nil;
            Items.clear();
            StructFields.clear();
            AsString.clear();
            Ar.SetHasError(true);
            return false;
        }

        const int64 SizePos = Ar.Tell();
        uint32 PayloadSize = 0;
        Ar << PayloadSize;
        const int64 BodyStart = Ar.Tell();

        uint8 KindRaw = (uint8)Kind;
        Ar << KindRaw;
        if (Ar.IsReading())
        {
            Kind = (EScriptExportKind)KindRaw;
        }

        const bool bKindKnown = KindRaw <= (uint8)EScriptExportKind::UnknownUserdata;

        if (Ar.IsReading() && !bKindKnown)
        {
            LOG_WARN("FScriptPropertyValue - unknown kind {}, skipping payload.", KindRaw);
            Ar.Seek(BodyStart + PayloadSize);
            Kind = EScriptExportKind::Nil;
            Items.clear();
            StructFields.clear();
            AsString.clear();
            return !Ar.HasError();
        }

        const size_t MaxSize = Ar.GetMaxSerializeSize();

        switch (Kind)
        {
        case EScriptExportKind::Nil:
            break;
        case EScriptExportKind::Bool:
            Ar << AsBool;
            break;
        case EScriptExportKind::Int:
            Ar << AsInt;
            break;
        case EScriptExportKind::Double:
            Ar << AsDouble;
            break;
        case EScriptExportKind::String:
            Ar << AsString;
            break;
        case EScriptExportKind::Vec2:
        case EScriptExportKind::Vec3:
        case EScriptExportKind::Vec4:
            Ar << AsVec.x;
            Ar << AsVec.y;
            Ar << AsVec.z;
            Ar << AsVec.w;
            break;
        case EScriptExportKind::UnknownUserdata:
            Ar << UserdataTypeName;
            break;
        case EScriptExportKind::Array:
        {
            uint32 Count = (uint32)Items.size();
            Ar << Count;
            if (Ar.IsReading())
            {
                if (Count > MaxSize)
                {
                    LOG_ERROR("FScriptPropertyValue::Array - count {} exceeds max {}.", Count, MaxSize);
                    Ar.SetHasError(true);
                    Ar.Seek(BodyStart + PayloadSize);
                    Kind = EScriptExportKind::Nil;
                    Items.clear();
                    return false;
                }
                Items.clear();
                Items.resize(Count);
            }
            for (uint32 i = 0; i < Count && !Ar.HasError(); ++i)
            {
                Items[i].Serialize(Ar);
            }
            break;
        }
        case EScriptExportKind::NestedStruct:
        {
            uint32 Count = (uint32)StructFields.size();
            Ar << Count;
            if (Ar.IsReading())
            {
                if (Count > MaxSize)
                {
                    LOG_ERROR("FScriptPropertyValue::Struct - count {} exceeds max {}.", Count, MaxSize);
                    Ar.SetHasError(true);
                    Ar.Seek(BodyStart + PayloadSize);
                    Kind = EScriptExportKind::Nil;
                    StructFields.clear();
                    return false;
                }
                StructFields.clear();
                StructFields.resize(Count);
            }
            for (uint32 i = 0; i < Count && !Ar.HasError(); ++i)
            {
                Ar << StructFields[i].Name;
                StructFields[i].Value.Serialize(Ar);
            }
            break;
        }
        default:
            break;
        }

        if (Ar.IsWriting())
        {
            const int64 EndPos = Ar.Tell();
            PayloadSize = (uint32)(EndPos - BodyStart);
            Ar.Seek(SizePos);
            Ar << PayloadSize;
            Ar.Seek(EndPos);
        }
        else
        {
            const int64 ExpectedEnd = BodyStart + PayloadSize;
            if (Ar.Tell() != ExpectedEnd)
            {
                // Realign on size mismatch so outer read stays sane.
                Ar.Seek(ExpectedEnd);
            }
        }

        return !Ar.HasError();
    }

    FScriptPropertyValue FScriptPropertyValue::FromType(const FScriptExportType& Type)
    {
        FScriptPropertyValue V;
        V.Kind = Type.Kind;
        switch (Type.Kind)
        {
        case EScriptExportKind::UnknownUserdata:
            V.UserdataTypeName = Type.UserdataTypeName;
            break;
        case EScriptExportKind::NestedStruct:
            V.StructFields.reserve(Type.Fields.size());
            for (const FScriptExportField& Field : Type.Fields)
            {
                FScriptPropertyEntry Entry;
                Entry.Name = Field.Name;
                if (Field.Type)
                {
                    Entry.Value = FromType(*Field.Type);
                }
                V.StructFields.emplace_back(eastl::move(Entry));
            }
            break;
        default:
            break;
        }
        return V;
    }

    static bool SameShape(const FScriptExportType& Type, const FScriptPropertyValue& Value)
    {
        if (Type.Kind != Value.Kind)
        {
            return false;
        }
        if (Type.Kind == EScriptExportKind::Array)
        {
            // Element-type mismatch handled on reapply.
            return true;
        }
        if (Type.Kind == EScriptExportKind::UnknownUserdata)
        {
            return Type.UserdataTypeName == Value.UserdataTypeName;
        }
        return true;
    }

    void ReconcileOverrides(const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Defaults, TVector<FScriptPropertyEntry>& InOutOverrides)
    {
        TVector<FScriptPropertyEntry> New;
        New.reserve(Schema.Fields.size());

        for (const FScriptExportField& Field : Schema.Fields)
        {
            if (!Field.Type) continue;

            const FScriptPropertyEntry* Existing = nullptr;
            for (const FScriptPropertyEntry& E : InOutOverrides)
            {
                if (E.Name == Field.Name) { Existing = &E; break; }
            }

            FScriptPropertyEntry Entry;
            Entry.Name = Field.Name;

            if (Existing && SameShape(*Field.Type, Existing->Value))
            {
                Entry.Value = Existing->Value;
            }
            else
            {
                // Fall back to script-default, then zero.
                const FScriptPropertyEntry* Default = nullptr;
                for (const FScriptPropertyEntry& D : Defaults)
                {
                    if (D.Name == Field.Name) { Default = &D; break; }
                }
                Entry.Value = Default ? Default->Value : FScriptPropertyValue::FromType(*Field.Type);
            }
            New.emplace_back(eastl::move(Entry));
        }

        InOutOverrides = eastl::move(New);
    }
}
