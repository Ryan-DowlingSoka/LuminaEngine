#include "pch.h"
#include "Name.h"

#include <iostream>
#include "Memory/Memory.h"


namespace Lumina
{
    FNameTable::FNameTable()
    {
        HashToString.reserve(INITIAL_CAPACITY);
        HashToString.insert_or_assign(0, "NAME_None");
    }

    uint64 FNameTable::GetOrCreateID(const char* Str)
    {
        if (!Str || !Str[0])
        {
            return 0;
        }

        const size_t Length = strlen(Str);
        const uint64 ID = Hash::XXHash::GetHash64(Str, Length);

        FScopeLock Lock(Mutex);
            
        auto It = HashToString.find(ID);
        if ((It != HashToString.end()))
        {
            return ID;
        }
            
        const char* PermanentStr = Pool.AllocateString(Str, Length);
            
        HashToString.insert_or_assign(ID, PermanentStr);
            
        return ID;
    }

    uint64 FNameTable::GetOrCreateID(const char* Str, size_t Length)
    {
        if (!Str || !Str[0])
        {
            return 0;
        }

        const uint64 ID = Hash::XXHash::GetHash64(Str, Length);

        FScopeLock Lock(Mutex);
            
        auto It = HashToString.find(ID);
        if (It != HashToString.end())
        {
            return ID;
        }
            
        const char* PermanentStr = Pool.AllocateString(Str, Length);
            
        HashToString.insert_or_assign(ID, PermanentStr);
            
        return ID;
    }

    const char* FNameTable::GetString(uint64 ID) const
    {
        auto It = HashToString.find(ID);
        return (It != HashToString.end()) ? It->second : nullptr;
    }

    size_t FNameTable::GetMemoryUsage() const
    {
        return HashToString.size() * (sizeof(uint64) + sizeof(char*)) + GetStringPoolUsage();
    }

    size_t FNameTable::GetStringPoolUsage() const
    {
        size_t Total = 0;
        for (FStringPool::Chunk* Chunk = Pool.Head; Chunk; Chunk = Chunk->Next)
        {
            Total += Chunk->Used;
        }
        return Total;
    }

    RUNTIME_API FNameTable* GNameTable = nullptr;

    const char* FStringPool::AllocateString(const char* Str, size_t Length)
    {
        size_t AlignedLength = (Length + 1 + 7) & ~7;
            
        if (!Current || Current->Used + AlignedLength > CHUNK_SIZE)
        {
            Current = Memory::New<Chunk>();
            Current->Next = Head;
            Head = Current;
        }
            
        char* Result = Current->Data + Current->Used;
        memcpy(Result, Str, Length);
        Result[Length] = '\0';
        Current->Used += AlignedLength;
            
        return Result;
    }

    FStringPool::~FStringPool()
    {
        while (Head)
        {
            Chunk* Next = Head->Next;
            Memory::Delete(Head);
            Head = Next;
        }
    }

    void FName::Initialize()
    {
        GNameTable = Memory::New<FNameTable>();
        std::cout << "[Lumina] - String ID (FName) System Initialized\n";
        std::cout.flush();
    }

    void FName::Shutdown()
    {
        Memory::Delete(GNameTable);
        GNameTable = nullptr;
    }

    FName::FName(EName Name)
        : ID((uint64)Name)
    {
        View = GNameTable->GetString(ID);
    }

    FName::FName(const char* Str)
    {
        ID = GNameTable->GetOrCreateID(Str);
        View = GNameTable->GetString(ID);
    }

    FName::FName(const char* Str, size_t Length)
    {
        ID = GNameTable->GetOrCreateID(Str, Length);
        View = GNameTable->GetString(ID);
    }

    char FName::At(size_t Pos) const
    {
        const char* Str = c_str();
        size_t Len = strlen(Str);
    
        if (Pos >= Len)
        {
            return '\0';
        }
    
        return Str[Pos];
    }
}
