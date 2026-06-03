#include "pch.h"
#include "ScriptExports.h"
#include "ScriptAnnotations.h"

#include "Core/Serialization/Archiver.h"
#include "Log/Log.h"
#include "lua.h"
#include "lualib.h"
#include <cstdlib>

namespace Lumina::Lua
{
    const FString* FScriptExportMeta::Find(const FName& Key) const
    {
        for (const FScriptAnnotationArg& Arg : Entries)
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
        for (FScriptAnnotationArg& Arg : Entries)
        {
            if (Arg.Key == Key)
            {
                Arg.Value = Value;
                return;
            }
        }
        Entries.push_back(FScriptAnnotationArg{ Key, Value });
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

    // Duck-typed schema + default builder. One runtime walk over the live `Exports` table.
    namespace
    {
        static TSharedPtr<FScriptExportType> BuildTypeFromLuaValue(lua_State* State, int Index, FScriptPropertyValue& OutValue);

        // Reads __typename stamped on the metatable by TClass; empty if none.
        static FName ReadUserdataTypeName(lua_State* State, int Index)
        {
            if (!lua_getmetatable(State, Index))
            {
                return {};
            }
            lua_rawgetfield(State, -1, "__typename");
            FName Result;
            if (lua_isstring(State, -1))
            {
                size_t Len = 0;
                const char* S = lua_tolstring(State, -1, &Len);
                Result = FName(FString(S, Len));
            }
            lua_pop(State, 2);
            return Result;
        }

        // Empty tables fall through as NestedStruct so the editor can still show a shape.
        static bool TableLooksLikeArray(lua_State* State, int Index)
        {
            Index = lua_absindex(State, Index);
            int Len = lua_objlen(State, Index);
            if (Len <= 0) return false;

            lua_pushnil(State);
            while (lua_next(State, Index) != 0)
            {
                int KeyType = lua_type(State, -2);
                lua_pop(State, 1);
                if (KeyType != LUA_TNUMBER)
                {
                    lua_pop(State, 1);
                    return false;
                }
            }
            return true;
        }

        static TSharedPtr<FScriptExportType> BuildArrayType(lua_State* State, int Index, FScriptPropertyValue& OutValue)
        {
            auto Type = MakeShared<FScriptExportType>();
            Type->Kind = EScriptExportKind::Array;
            OutValue.Kind = EScriptExportKind::Array;

            Index = lua_absindex(State, Index);
            int Len = lua_objlen(State, Index);
            OutValue.Items.reserve((size_t)Len);

            for (int i = 1; i <= Len; ++i)
            {
                lua_rawgeti(State, Index, i);
                FScriptPropertyValue Elem;
                auto ElemType = BuildTypeFromLuaValue(State, -1, Elem);
                if (!Type->ElementType && ElemType)
                {
                    Type->ElementType = ElemType;
                }
                OutValue.Items.emplace_back(eastl::move(Elem));
                lua_pop(State, 1);
            }

            if (!Type->ElementType)
            {
                auto Nil = MakeShared<FScriptExportType>();
                Nil->Kind = EScriptExportKind::Nil;
                Type->ElementType = Nil;
            }
            return Type;
        }

        static TSharedPtr<FScriptExportType> BuildStructType(lua_State* State, int Index, FScriptPropertyValue& OutValue)
        {
            auto Type = MakeShared<FScriptExportType>();
            Type->Kind = EScriptExportKind::NestedStruct;
            OutValue.Kind = EScriptExportKind::NestedStruct;

            Index = lua_absindex(State, Index);

            lua_pushnil(State);
            while (lua_next(State, Index) != 0)
            {
                if (lua_type(State, -2) == LUA_TSTRING)
                {
                    size_t KeyLen = 0;
                    const char* KeyStr = lua_tolstring(State, -2, &KeyLen);
                    FName FieldName(FString(KeyStr, KeyLen));

                    FScriptPropertyValue FieldValue;
                    auto FieldType = BuildTypeFromLuaValue(State, -1, FieldValue);

                    FScriptExportField Field;
                    Field.Name = FieldName;
                    Field.Type = FieldType;
                    Type->Fields.emplace_back(eastl::move(Field));

                    FScriptPropertyEntry Entry;
                    Entry.Name = FieldName;
                    Entry.Value = eastl::move(FieldValue);
                    OutValue.StructFields.emplace_back(eastl::move(Entry));
                }
                lua_pop(State, 1);
            }

            return Type;
        }

        static TSharedPtr<FScriptExportType> BuildTypeFromLuaValue(lua_State* State, int Index, FScriptPropertyValue& OutValue)
        {
            auto Nil = [&]() {
                auto P = MakeShared<FScriptExportType>();
                P->Kind = EScriptExportKind::Nil;
                OutValue.Kind = EScriptExportKind::Nil;
                return P;
            };

            switch (lua_type(State, Index))
            {
            case LUA_TBOOLEAN:
            {
                auto P = MakeShared<FScriptExportType>();
                P->Kind = EScriptExportKind::Bool;
                OutValue.Kind = EScriptExportKind::Bool;
                OutValue.AsBool = lua_toboolean(State, Index) != 0;
                return P;
            }
            case LUA_TNUMBER:
            {
                double D = (double)lua_tonumber(State, Index);
                int64  I = (int64)lua_tointeger(State, Index);
                auto P = MakeShared<FScriptExportType>();
                if ((double)I == D)
                {
                    P->Kind = EScriptExportKind::Int;
                    OutValue.Kind = EScriptExportKind::Int;
                    OutValue.AsInt = I;
                }
                else
                {
                    P->Kind = EScriptExportKind::Double;
                    OutValue.Kind = EScriptExportKind::Double;
                    OutValue.AsDouble = D;
                }
                return P;
            }
            case LUA_TSTRING:
            {
                size_t Len = 0;
                const char* S = lua_tolstring(State, Index, &Len);
                auto P = MakeShared<FScriptExportType>();
                P->Kind = EScriptExportKind::String;
                OutValue.Kind = EScriptExportKind::String;
                OutValue.AsString.assign(S, Len);
                return P;
            }
            case LUA_TVECTOR:
            {
                const float* Vec = lua_tovector(State, Index);
                auto P = MakeShared<FScriptExportType>();
                P->Kind = EScriptExportKind::Vec3;
                OutValue.Kind = EScriptExportKind::Vec3;
                if (Vec)
                {
                    OutValue.AsVec.x = Vec[0];
                    OutValue.AsVec.y = Vec[1];
                    OutValue.AsVec.z = Vec[2];
                }
                return P;
            }
            case LUA_TUSERDATA:
            {
                auto P = MakeShared<FScriptExportType>();
                P->Kind = EScriptExportKind::UnknownUserdata;
                P->UserdataTypeName = ReadUserdataTypeName(State, Index);
                OutValue.Kind = EScriptExportKind::UnknownUserdata;
                OutValue.UserdataTypeName = P->UserdataTypeName;
                return P;
            }
            case LUA_TTABLE:
            {
                if (TableLooksLikeArray(State, Index))
                {
                    return BuildArrayType(State, Index, OutValue);
                }
                return BuildStructType(State, Index, OutValue);
            }
            default:
                return Nil();
            }
        }
    }

    bool BuildSchemaFromAnnotatedExports(
        lua_State* State,
        int ScriptTableIndex,
        const TVector<FScriptMemberAnnotation>& Annotations,
        FScriptExportSchema& OutSchema,
        TVector<FScriptPropertyEntry>& OutDefaults)
    {
        OutSchema.Fields.clear();
        OutDefaults.clear();

        if (!lua_istable(State, ScriptTableIndex))
        {
            return false;
        }

        ScriptTableIndex = lua_absindex(State, ScriptTableIndex);

        for (const FScriptMemberAnnotation& Annotation : Annotations)
        {
            if (!Annotation.bExport || Annotation.bIsFunction)
            {
                continue; // only exported, non-function members are editable properties
            }

            bool bAlready = false;
            for (const FScriptExportField& Existing : OutSchema.Fields)
            {
                if (Existing.Name == Annotation.Member) { bAlready = true; break; }
            }
            if (bAlready)
            {
                continue;
            }

            // Read the member's default straight off the script table (raw, no metatable).
            const FString MemberStr = Annotation.Member.ToString();
            lua_rawgetfield(State, ScriptTableIndex, MemberStr.c_str());
            if (lua_isnil(State, -1))
            {
                lua_pop(State, 1);
                LOG_WARN("[Script] --@export '{}' has no assigned value (Script.{} = ...); skipping.", MemberStr, MemberStr);
                continue;
            }

            FScriptPropertyValue Value;
            auto Type = BuildTypeFromLuaValue(State, -1, Value);
            lua_pop(State, 1);

            FScriptExportField Field;
            Field.Name = Annotation.Member;
            Field.Type = Type;
            for (const FScriptAnnotationArg& Arg : Annotation.ExportMeta)
            {
                Field.Meta.Set(Arg.Key, Arg.Value);
            }
            OutSchema.Fields.emplace_back(eastl::move(Field));

            FScriptPropertyEntry Entry;
            Entry.Name = Annotation.Member;
            Entry.Value = eastl::move(Value);
            OutDefaults.emplace_back(eastl::move(Entry));
        }

        return !OutSchema.Fields.empty();
    }

    static void PushValueToLua(lua_State* State, const FScriptPropertyValue& Value, const FScriptExportType& Type);

    static void PushArrayToLua(lua_State* State, const FScriptPropertyValue& Value, const FScriptExportType& Type)
    {
        lua_createtable(State, (int)Value.Items.size(), 0);
        if (!Type.ElementType) return;
        for (size_t i = 0; i < Value.Items.size(); ++i)
        {
            PushValueToLua(State, Value.Items[i], *Type.ElementType);
            lua_rawseti(State, -2, (int)(i + 1));
        }
    }

    static void PushStructToLua(lua_State* State, const FScriptPropertyValue& Value, const FScriptExportType& Type)
    {
        lua_createtable(State, 0, (int)Type.Fields.size());
        for (const FScriptExportField& Field : Type.Fields)
        {
            if (!Field.Type) continue;
            const FScriptPropertyValue* FieldValue = nullptr;
            for (const FScriptPropertyEntry& Entry : Value.StructFields)
            {
                if (Entry.Name == Field.Name) { FieldValue = &Entry.Value; break; }
            }
            FScriptPropertyValue Fallback;
            if (!FieldValue)
            {
                Fallback = FScriptPropertyValue::FromType(*Field.Type);
                FieldValue = &Fallback;
            }
            PushValueToLua(State, *FieldValue, *Field.Type);
            FString KeyStr = Field.Name.ToString();
            lua_setfield(State, -2, KeyStr.c_str());
        }
    }

    static void PushValueToLua(lua_State* State, const FScriptPropertyValue& Value, const FScriptExportType& Type)
    {
        switch (Type.Kind)
        {
        case EScriptExportKind::Nil:
            lua_pushnil(State);
            return;
        case EScriptExportKind::Bool:
            lua_pushboolean(State, Value.AsBool ? 1 : 0);
            return;
        case EScriptExportKind::Int:
            lua_pushinteger(State, (int)Value.AsInt);
            return;
        case EScriptExportKind::Double:
            lua_pushnumber(State, Value.AsDouble);
            return;
        case EScriptExportKind::String:
            lua_pushlstring(State, Value.AsString.c_str(), Value.AsString.size());
            return;
        case EScriptExportKind::Vec2:
        case EScriptExportKind::Vec3:
        case EScriptExportKind::Vec4:
            lua_pushvector(State, Value.AsVec.x, Value.AsVec.y, Value.AsVec.z, Value.AsVec.w);
            return;
        case EScriptExportKind::UnknownUserdata:
            // v1: not editable.
            lua_pushnil(State);
            return;
        case EScriptExportKind::Array:
            PushArrayToLua(State, Value, Type);
            return;
        case EScriptExportKind::NestedStruct:
            PushStructToLua(State, Value, Type);
            return;
        }
    }

    void ApplyOverridesToScriptTable(lua_State* State, int ScriptTableIndex, const FScriptExportSchema& Schema, const TVector<FScriptPropertyEntry>& Overrides)
    {
        if (!lua_istable(State, ScriptTableIndex)) return;
        ScriptTableIndex = lua_absindex(State, ScriptTableIndex);

        for (const FScriptPropertyEntry& Entry : Overrides)
        {
            const FScriptExportField* Field = nullptr;
            for (const FScriptExportField& F : Schema.Fields)
            {
                if (F.Name == Entry.Name) { Field = &F; break; }
            }
            if (!Field || !Field->Type) continue;
            if (Field->Type->Kind == EScriptExportKind::UnknownUserdata)
            {
                // v1: userdata overrides not applied; default stays.
                continue;
            }

            PushValueToLua(State, Entry.Value, *Field->Type);
            FString KeyStr = Entry.Name.ToString();
            lua_setfield(State, ScriptTableIndex, KeyStr.c_str());
        }
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
