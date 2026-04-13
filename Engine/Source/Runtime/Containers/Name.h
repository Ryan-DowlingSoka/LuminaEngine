#pragma once

#include "String.h"
#include "Core/DisableAllWarnings.h"
#include "Core/LuminaMacros.h"
#include "Core/Assertions/Assert.h"
#include "Core/Math/Hash/Hash.h"
#include "Core/Templates/CanBulkSerialize.h"
#include "Core/Threading/Thread.h"

enum class EName : uint32;
PRAGMA_DISABLE_ALL_WARNINGS
#include "EASTL/hash_map.h"
PRAGMA_ENABLE_ALL_WARNINGS

#include "Platform/GenericPlatform.h"

namespace Lumina
{
    class FStringPool
    {
    public:
        static constexpr size_t CHUNK_SIZE = 1024 * 1024; // 1MB chunks
        
        struct Chunk
        {
            alignas(64) char Data[CHUNK_SIZE];
            size_t Used = 0;
            Chunk* Next = nullptr;
        };
        
        Chunk* Head = nullptr;
        Chunk* Current = nullptr;
        
        const char* AllocateString(const char* Str, size_t Length);
        
        ~FStringPool();
    };

    class FNameTable
    {
    public:
        
        FNameTable();
        
        uint64 GetOrCreateID(const char* Str);
        uint64 GetOrCreateID(const char* Str, size_t Length);
        
        const char* GetString(uint64 ID) const;
        size_t GetMemoryUsage() const;
        
    private:
        
        size_t GetStringPoolUsage() const;
        
    private:
        
        FMutex Mutex;
        eastl::hash_map<uint64, const char*> HashToString;
        FStringPool Pool;
        
        static constexpr size_t INITIAL_CAPACITY = 16384;
        
    };

    extern RUNTIME_API FNameTable* GNameTable;
    
    class RUNTIME_API FName
    {
    public:

        // Helps with string API concepts.
        using value_type = char;
        
        static void Initialize();

        static void Shutdown();

    public:
        
        FName() = default;
        FName(EName Name);
        
        FName(const char* Str);
        FName(const char* Str, size_t Length);

        FName(const TCHAR* Str) : FName(StringUtils::FromWideString(Str)) {}
        FName(const FString& Str) : FName(Str.data(), Str.length()) {}
        FName(const FWString& Str) : FName(StringUtils::FromWideString(Str)) {}
        FName(const FFixedString& Str) : FName(Str.data(), Str.length()) {}
        FName(const FFixedWString& Str) : FName(Str.c_str()) {}
        FName(FStringView Str) : FName(Str.data(), Str.length()) {}

        explicit FName(uint64 InID) : ID(InID) {}

        bool IsNone() const { return ID == 0; }
        uint64 GetID() const { return ID; }
        operator uint64() const { return ID; }

        const char* c_str() const
        {
            return GNameTable->GetString(ID);
        }
        
        char At(size_t Pos) const;
        
        FString ToString() const
        {
            const char* Str = c_str();
            DEBUG_ASSERT(Str);
            
            return Str;
        }

        size_t Length() const
        {
            return strlen(GNameTable->GetString(ID));
        }
        
        // For stl.
        size_t length() const
        {
            return strlen(GNameTable->GetString(ID));
        }

        FName& operator=(const EName InName) { *this = FName(InName); return *this; }
        
        bool operator==(const FName& Other) const { return ID == Other.ID; }
        bool operator!=(const FName& Other) const { return ID != Other.ID; }
        bool operator<(const FName& Other) const { return ID < Other.ID; }
        bool operator<=(const FName& Other) const { return ID <= Other.ID; }
        bool operator>(const FName& Other) const { return ID > Other.ID; }
        bool operator>=(const FName& Other) const { return ID >= Other.ID; }
    
        bool operator==(const EName Name) const { return ID == (uint64)Name; }
        bool operator!=(const EName Name) const { return ID != (uint64)Name; }
        
        size_t hash() const { return ID; }

        
        const char* operator * () const
        {
            return c_str();
        }
    
    
    private:
        
        FStringView View;
        uint64 ID = 0;
    };
    
    template<>
    struct TCanBulkSerialize<FName> : eastl::false_type {};
    
}

namespace eastl
{
    template <typename T> struct hash;

    template <>
    struct eastl::hash<Lumina::FName>
    {
        size_t operator()(const Lumina::FName& Name) const
        {
            return Name.hash();
        }
    };
}

template <>
struct std::formatter<Lumina::FName>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Lumina::FName& str, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "{}", std::string_view(str.c_str()));
    }
};
