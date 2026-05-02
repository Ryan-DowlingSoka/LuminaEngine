#pragma once

#include "Core/Assertions/Assert.h"
#include "Memory/Memory.h"

namespace Lumina
{

    class RUNTIME_API IAllocator
    {
    public:

        virtual ~IAllocator() = default;
        
        template<typename T, typename... Args>
        T* TAlloc(Args&&... args)
        {
            void* Mem = Allocate(sizeof(T), alignof(T));
            return new (Mem) T(Forward<Args>(args)...);
        } 
        
        virtual void* Allocate(size_t Size, size_t Alignment = alignof(std::max_align_t)) = 0;
        virtual void Free(void* Data) = 0;
        virtual size_t GetCapacity() { return 0; }
        virtual void Reset() = 0;
    };

    class RUNTIME_API FDefaultAllocator : public IAllocator
    {
    public:
        
        
        void* Allocate(size_t Size, size_t Alignment) override
        {
            return Memory::Malloc(Size, Alignment);
        }
        
        void Free(void* Data) override
        {
            Memory::Free(Data);
        }
        
        void Reset() override { }
    };
    
    class RUNTIME_API FLinearAllocator : public IAllocator
    {
    public:
        
        explicit FLinearAllocator(size_t InCapacity)
        {
            Capacity = InCapacity;
            Base = (uint8*)Memory::Malloc(Capacity);
            Offset = 0;
        }

        ~FLinearAllocator() override
        {
            Memory::Free(Base);
            Base = nullptr;
        }
        
        void* Allocate(size_t Size, size_t Alignment) override
        {
            size_t CurrentPtr = reinterpret_cast<size_t>(Base + Offset);
            SIZE_T AlignedPtr = (CurrentPtr + Alignment - 1) & ~(Alignment - 1);
            SIZE_T NextOffset = AlignedPtr - reinterpret_cast<SIZE_T>(Base) + Size;
            ASSERT(NextOffset < Capacity);
            
            void* Result = Base + (AlignedPtr - reinterpret_cast<SIZE_T>(Base));
            Offset = NextOffset;
            return Result;
        }

        void Free(void* Data) override { }

        void Reset() override
        {
            Offset = 0;
        }

        SIZE_T GetCapacity() override { return Capacity; }
        SIZE_T GetUsed() const { return Offset; }

    private:
        
        uint8* Base = nullptr;
        SIZE_T Offset = 0;
        SIZE_T Capacity = 0;
    };

    
    class RUNTIME_API FBlockLinearAllocator : public IAllocator 
    {
    public:
        
        explicit FBlockLinearAllocator(const char* AllocatorName) noexcept
            :FBlockLinearAllocator()
        {}
        
        FBlockLinearAllocator() noexcept
            : BlockSize(1024)
            , CurrentOffset(0)
            , BlockCount(0)
        { 
            AllocateNewBlock();
        } 
        
        explicit FBlockLinearAllocator(SIZE_T InBlockSize) 
            : BlockSize(InBlockSize)
            , CurrentOffset(0)
            , BlockCount(0)
        { 
            AllocateNewBlock();
        } 
    
        ~FBlockLinearAllocator() override 
        { 
            Block* Current = FirstBlock;
            while (Current)
            {
                Block* Next = Current->Next;
                Memory::Free(Current);
                Current = Next;
            }
        }

        void* allocate(size_t n, int flags = 0)
        {
            return Allocate(n, EASTL_ALLOCATOR_MIN_ALIGNMENT);
        }
        
        void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0)
        {
            return Allocate(n, alignment);
        }
        
        void deallocate(void* p, size_t n)
        {
        }
         
        void* Allocate(SIZE_T Size, SIZE_T Alignment) override
        {
            ASSERT(Size < GetUsableBlockSize());

            SIZE_T CurrentPtr = reinterpret_cast<SIZE_T>(CurrentBlock->GetData() + CurrentOffset);
            SIZE_T AlignedPtr = (CurrentPtr + Alignment - 1) & ~(Alignment - 1);
            SIZE_T NextOffset = AlignedPtr - reinterpret_cast<SIZE_T>(CurrentBlock->GetData()) + Size;

            if (NextOffset > GetUsableBlockSize())
            {
                AllocateNewBlock();
                CurrentOffset = 0;

                CurrentPtr = reinterpret_cast<SIZE_T>(CurrentBlock->GetData() + CurrentOffset);
                AlignedPtr = (CurrentPtr + Alignment - 1) & ~(Alignment - 1);
                NextOffset = AlignedPtr - reinterpret_cast<SIZE_T>(CurrentBlock->GetData()) + Size;

                ASSERT(NextOffset <= GetUsableBlockSize());
            }

            void* Result = CurrentBlock->GetData() + (AlignedPtr - reinterpret_cast<SIZE_T>(CurrentBlock->GetData()));
            CurrentOffset = NextOffset;
            return Result;
        }
    
        void Free(void* Data) override { }
    
        void Reset() override 
        { 
            CurrentBlock = FirstBlock;
            CurrentOffset = 0;
        }

        /** Frees all blocks except the first, then resets. */
        void Compact()
        {
            if (!FirstBlock)
            {
                return;
            }

            Block* Current = FirstBlock->Next;
            while (Current)
            {
                Block* Next = Current->Next;
                Memory::Free(Current);
                Current = Next;
                BlockCount--;
            }

            FirstBlock->Next = nullptr;
            CurrentBlock = FirstBlock;
            CurrentOffset = 0;
        }
    
        SIZE_T GetCapacity() override 
        { 
            return BlockCount * BlockSize; 
        }
        
        SIZE_T GetUsed() const 
        { 
            SIZE_T Used = 0;

            Block* Block = FirstBlock;
            while (Block != CurrentBlock && Block != nullptr)
            {
                Used += GetUsableBlockSize();
                Block = Block->Next;
            }

            if (CurrentBlock)
            {
                Used += CurrentOffset;
            }
            
            return Used;
        }
        
        SIZE_T GetBlockCount() const 
        { 
            return BlockCount; 
        }
    
    private:
        
        struct Block
        {
            Block* Next;
            
            uint8* GetData() { return reinterpret_cast<uint8*>(this + 1); }
            const uint8* GetData() const { return reinterpret_cast<const uint8*>(this + 1); }
        };
        
        SIZE_T GetUsableBlockSize() const
        {
            return BlockSize - sizeof(Block);
        }
        
        void AllocateNewBlock()
        {
            // Reuse a chained block from before Reset(); allocate fresh only at tail.
            if (CurrentBlock && CurrentBlock->Next)
            {
                CurrentBlock = CurrentBlock->Next;
                return;
            }

            Block* NewBlock = (Block*)Memory::Malloc(BlockSize);
            ASSERT(NewBlock != nullptr);

            NewBlock->Next = nullptr;

            if (CurrentBlock)
            {
                CurrentBlock->Next = NewBlock;
            }
            else
            {
                FirstBlock = NewBlock;
            }

            CurrentBlock = NewBlock;
            BlockCount++;
        }
        
        Block* FirstBlock = nullptr;
        Block* CurrentBlock = nullptr;
        SIZE_T BlockSize;
        SIZE_T CurrentOffset;
        SIZE_T BlockCount;
    };


    /** EASTL adapter; copyable handle to an external FBlockLinearAllocator. Containers must not outlive the arena. */
    class FFrameArenaAllocator
    {
    public:
        FFrameArenaAllocator(const char* InName = "frame") noexcept
            : Arena(nullptr), Name(InName) {}

        explicit FFrameArenaAllocator(FBlockLinearAllocator* InArena, const char* InName = "frame") noexcept
            : Arena(InArena), Name(InName) {}

        void* allocate(size_t n, int /*flags*/ = 0)
        {
            ASSERT(Arena != nullptr);
            return Arena->Allocate(n, 16);
        }

        void* allocate(size_t n, size_t alignment, size_t /*offset*/, int /*flags*/ = 0)
        {
            ASSERT(Arena != nullptr);
            return Arena->Allocate(n, alignment);
        }

        void deallocate(void* /*p*/, size_t /*n*/) noexcept {}

        const char* get_name() const           { return Name; }
        void        set_name(const char* InN)  { Name = InN; }

        FBlockLinearAllocator* GetArena() const { return Arena; }

        bool operator==(const FFrameArenaAllocator& Other) const { return Arena == Other.Arena; }
        bool operator!=(const FFrameArenaAllocator& Other) const { return Arena != Other.Arena; }

    private:
        FBlockLinearAllocator* Arena;
        const char*            Name;
    };

}