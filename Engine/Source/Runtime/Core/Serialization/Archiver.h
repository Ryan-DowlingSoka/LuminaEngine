#pragma once

#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Object/ObjectHandle.h"
#include "Core/Templates/CanBulkSerialize.h"
#include "Core/Templates/IsSigned.h"
#include "Core/Versioning/CoreVersion.h"
#include "glm/glm.hpp"
#include <glm/gtc/quaternion.hpp>

#include "EASTL/bit.h"
#include "Log/Log.h"
#include "Types/BitFlags.h"

namespace Lumina
{
    class FField;
}

enum class EArchiverFlags : uint8
{
    None        = 0,
    Reading     = 1,
    Writing     = 2,
    Compress    = 3,
    Encrypt     = 4,
    NoFields    = 5,
};

namespace Lumina
{
    class CObject;

    class RUNTIME_API FArchive
    {
    public:
    
        FArchive() = default;
        FArchive(const FArchive&) = delete;
        FArchive& operator=(const FArchive& ArchiveToCopy) = delete;
        virtual ~FArchive() = default;

    
        virtual void Seek(int64 InPos) { }
        virtual int64 Tell() { return 0; }
        virtual int64 TotalSize() { return 0; }

        virtual void Serialize(void* V, int64 Length) {}

        FORCEINLINE void SetFlag(EArchiverFlags Flag) { Flags.SetFlag(Flag); }
        FORCEINLINE void RemoveFlag(EArchiverFlags Flag) { Flags.ClearFlag(Flag); }
        FORCEINLINE bool HasFlag(EArchiverFlags Flag) const { return Flags.IsFlagSet(Flag); }

        FORCEINLINE bool virtual IsWriting() const { return HasFlag(EArchiverFlags::Writing); }

        /** Reading from a buffer (i.e. deserializing into a class). */
        FORCEINLINE bool virtual IsReading() const { return HasFlag(EArchiverFlags::Reading); }

        FORCEINLINE void SetHasError(bool bIsError) { bHasError = bIsError; }
        FORCEINLINE virtual bool HasError() const { return bHasError; }

        FORCEINLINE static FPackageFileVersion GetEngineVersion()
        {
            return GPackageFileLuminaVersion;
        }

        /**
         * Version of the file this archive is reading/writing. Defaults to the current engine
         * version; LoadPackage overrides it after reading the header so per-type Serialize
         * implementations can branch on the source version to migrate older payloads.
         */
        FORCEINLINE int32 GetFileVersion() const { return FileVersion; }
        FORCEINLINE void SetFileVersion(int32 InVersion) { FileVersion = InVersion; }

        FORCEINLINE size_t GetMaxSerializeSize() const { return ArMaxSerializeSize; }

        
        virtual FArchive& operator<<(CObject*& Value)
        {
            LOG_ERROR("Serializing CObjects is not supported by this archive.");
            return *this;
        }

        virtual FArchive& operator<<(FObjectHandle& Value)
        {
            LOG_ERROR("Serializing CObjects is not supported by this archive.");
            return *this;
        }

        virtual FArchive& operator<<(FField*& Value)
        {
            LOG_ERROR("Serializing FFields is not supported by this archive.");
            return *this;
        }
    
        virtual FArchive& operator<<(uint8& Value)
        {
            Serialize(&Value, 1);
            return *this;
        }

        virtual FArchive& operator<<(int8& Value)
        {
            Serialize(&Value, 1);
            return *this;
        }
    
        virtual FArchive& operator<<(uint16& Value)
        {
            ByteOrderSerialize(Value);
            return *this;
        }
    
        virtual FArchive& operator<<(int16& Value)
        {
            ByteOrderSerialize(reinterpret_cast<uint16&>(Value));
            return *this;
        }
    
        virtual FArchive& operator<<(uint32& Value)
        {
            ByteOrderSerialize(Value);
            return *this;
        }
    
        virtual FArchive& operator<<(int32& Value)
        {
            ByteOrderSerialize(reinterpret_cast<uint32&>(Value));
            return *this;
        }

        virtual FArchive& operator<<( bool& D)
        {
            SerializeBool(D);
            return *this;
        }

        virtual FArchive& operator<<(float& Value)
        {
            static_assert(sizeof(float) == sizeof(uint32), "Unexpected float size");
            uint32 Temp = std::bit_cast<uint32>(Value);
            ByteOrderSerialize(Temp);
            Value = std::bit_cast<float>(Temp);
            return *this;
        }

        virtual FArchive& operator<<(double& Value)
        {
            static_assert(sizeof(double) == sizeof(uint64), "Unexpected double size");
            uint64 Temp = std::bit_cast<uint64>(Value);
            ByteOrderSerialize(Temp);
            Value = std::bit_cast<double>(Temp);
            return *this;
        }
        
        virtual FArchive& operator<<(uint64& Value)
        {
            ByteOrderSerialize(Value);
            return *this;
        }

        virtual FArchive& operator<<(int64& Value)
        {
            ByteOrderSerialize(reinterpret_cast<uint64&>(Value));
            return *this;
        }

        virtual void SerializeBool(bool& D);
        
        virtual FArchive& operator<<(FString& Str)
        {
            if (IsReading())
            {
                size_t SaveNum = 0;
                *this << SaveNum;
            
                if (SaveNum > GetMaxSerializeSize())
                {
                    SetHasError(true);
                    LOG_ERROR("Archiver is corrupted, string is too large! (Size: {0}, Max: {1})", SaveNum, GetMaxSerializeSize());
                    return *this;
                }
            
                if (SaveNum)
                {
                    Str.clear();
                    Str.shrink_to_fit();
                    Str.resize(SaveNum);
                    Serialize(Str.data(), SaveNum);
                }
                else
                {
                    Str.clear();
                }
            }
            else
            {
                size_t SaveNum = Str.size();
                *this << SaveNum;

                if (SaveNum)
                {
                    Serialize(Str.data(), SaveNum);
                }
            }
        
            return *this;
        }
        
        virtual FArchive& operator<<(FFixedString& Str)
        {
            if (IsReading())
            {
                size_t SaveNum = 0;
                *this << SaveNum;
            
                if (SaveNum > GetMaxSerializeSize())
                {
                    SetHasError(true);
                    LOG_ERROR("Archiver is corrupted, string is too large! (Size: {0}, Max: {1})", SaveNum, GetMaxSerializeSize());
                    return *this;
                }
            
                if (SaveNum)
                {
                    Str.clear();
                    Str.shrink_to_fit();
                    Str.resize(SaveNum);
                    Serialize(Str.data(), SaveNum);
                }
                else
                {
                    Str.clear();
                }
            }
            else
            {
                size_t SaveNum = Str.size();
                *this << SaveNum;

                if (SaveNum)
                {
                    Serialize(Str.data(), SaveNum);
                }
            }
        
            return *this;
        }

        virtual FArchive& operator<<(FName& Name)
        {
            if (IsReading())
            {
                FString LoadedString;
                *this << LoadedString;
                Name = FName(LoadedString);
            }
            else
            {
                FString SavedString(Name.c_str());
                *this << SavedString;
            }

            return *this;
        }

        template<typename ValueType>
        FArchive& operator << (TVector<ValueType>& Array)
        {
            size_t SerializeNum = IsReading() ? 0 : Array.size();
            *this << SerializeNum;
        
            if (SerializeNum == 0)
            {
                if (IsReading())
                {
                    Array.clear();
                    Array.shrink_to_fit();
                }
            
                return *this;
            }
        
            if (SerializeNum > GetMaxSerializeSize())
            {
                SetHasError(true);
                LOG_ERROR("Archiver is corrupted, attempted to serialize {} array elements. Max is: {}", SerializeNum, GetMaxSerializeSize());
                return *this;
            }
            
            if constexpr (sizeof(ValueType) == 1 || TCanBulkSerialize<ValueType>::value)
            {
                if (IsReading())
                {
                    Array.clear();
                    Array.shrink_to_fit();
                    Array.resize(SerializeNum);
                }
            
                Serialize(Array.data(), SerializeNum * sizeof(ValueType));
            }
            else
            {
                if (IsReading())
                {
                    Array.clear();
                    Array.shrink_to_fit();
                    Array.resize(SerializeNum);
                }

                for (size_t i = 0; i < SerializeNum; i++)
                {
                    *this << Array[i];
                }
            }

            return *this;
        }
        
        template<typename K, typename V>
        FArchive& operator << (TPair<K, V>& Pair)
        {
            *this << Pair.first;
            *this << Pair.second;
            
            return *this;
        }
        
        template<typename K, typename V>
        FArchive& operator<<(THashMap<K, V>& Map)
        {
            uint32 Count;

            if (IsWriting())
            {
                Count = static_cast<uint32>(Map.size());
                *this << Count;

                for (TPair<K, V> Pair : Map)
                {
                    *this << Pair;
                }
            }
            else
            {
                *this << Count;

                if (Count > GetMaxSerializeSize())
                {
                    SetHasError(true);
                    LOG_ERROR("Archiver is corrupted, attempted to serialize {} map entries. Max is: {}", Count, GetMaxSerializeSize());
                    return *this;
                }

                Map.clear();
                Map.reserve(Count);

                for (uint32 i = 0; i < Count; ++i)
                {
                    TPair<K, V> Pair;
                    *this << Pair;
                    Map.emplace(Pair.first, std::move(Pair.second));
                }
            }

            return *this;
        }
        
        template<typename EnumType>
        FORCEINLINE FArchive& operator<<(EnumType& Value)
        requires (eastl::is_enum_v<EnumType>)
        {
            using Underlying = eastl::underlying_type_t<EnumType>;
            return *this << reinterpret_cast<Underlying&>(Value);
        }

    private:
    
        template<typename T>
        requires(!TIsSigned<T>::Value)
        FArchive& ByteOrderSerialize(T& Value)
        {
            Serialize(&Value, sizeof(T));
            return *this;
        }
    
    private:

        size_t                      ArMaxSerializeSize = INT32_MAX;

        int32                       FileVersion = GPackageFileLuminaVersion.FileVersion;

        TBitFlags<EArchiverFlags>   Flags;
        uint8                       bHasError:1 = false;

    };

    inline void FArchive::SerializeBool(bool& D)
    {
        uint8 BoolValue = D ? 1 : 0;
        Serialize(&BoolValue, sizeof(BoolValue));

        if (BoolValue > 1)
        {
            LOG_ERROR("Invalid boolean encountered while reading archive - stream is most likely corrupted.");
            SetHasError(true);
        }
        D = !!BoolValue;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::vec2& v)
    {
        Ar << v.x << v.y;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::vec3& v)
    {
        Ar << v.x << v.y << v.z;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::vec4& v)
    {
        Ar << v.x << v.y << v.z << v.w;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::ivec2& v)
    {
        Ar << v.x << v.y;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::ivec3& v)
    {
        Ar << v.x << v.y << v.z;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::ivec4& v)
    {
        Ar << v.x << v.y << v.z << v.w;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::uvec2& v)
    {
        Ar << v.x << v.y;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::uvec3& v)
    {
        Ar << v.x << v.y << v.z;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::uvec4& v)
    {
        Ar << v.x << v.y << v.z << v.w;
        return Ar;
    }
    
    inline FArchive& operator<<(FArchive& Ar, glm::u8vec2& v)
    {
        Ar << v.x << v.y;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::u8vec3& v)
    {
        Ar << v.x << v.y << v.z;
        return Ar;
    }
    
    inline FArchive& operator<<(FArchive& Ar, glm::u8vec4& v)
    {
        Ar << v.x << v.y << v.z << v.w;
        return Ar;
    }
    
    inline FArchive& operator<<(FArchive& Ar, glm::u16vec2& v)
    {
        Ar << v.x << v.y;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::u16vec3& v)
    {
        Ar << v.x << v.y << v.z;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::u16vec4& v)
    {
        Ar << v.x << v.y << v.z << v.w;
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::mat2& m)
    {
        Ar << m[0] << m[1];
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::mat3& m)
    {
        Ar << m[0] << m[1] << m[2];
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::mat4& m)
    {
        Ar << m[0] << m[1] << m[2] << m[3];
        return Ar;
    }

    inline FArchive& operator<<(FArchive& Ar, glm::quat& q)
    {
        Ar << q.x << q.y << q.z << q.w;
        return Ar;
    }

    inline FArchive& operator << (FArchive& Ar, FBitFlags& Data)
    {
        Ar << Data.Flags;
        return Ar;
    }
    
    
}





