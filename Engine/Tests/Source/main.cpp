#include "gtest/gtest.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectBase.h"
#include "EASTL/allocator.h"
#include "Log/Log.h"
#include "Memory/Memory.h"

class EngineTestEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        Lumina::Memory::Initialize();
        
        Lumina::Threading::Initialize("Main Thread");
        Lumina::FName::Initialize();
        Lumina::Logging::Init();

        Lumina::InitializeCObjectSystem();
    }

    void TearDown() override
    {
        Lumina::Logging::Shutdown();
        Lumina::FName::Shutdown();
        Lumina::Threading::Shutdown();
        Lumina::ShutdownCObjectSystem();
        
    }
};



int main(int Argc, char** Argv)
{
    ::testing::InitGoogleTest(&Argc, Argv);
    ::testing::AddGlobalTestEnvironment(new EngineTestEnvironment());
    return RUN_ALL_TESTS();
}

namespace eastl
{
    allocator GDefaultAllocator;

    allocator* GetDefaultAllocator()
    {
        return &GDefaultAllocator;
    }

    allocator* SetDefaultAllocator(allocator* pAllocator)
    {
        return &GDefaultAllocator;
    }

    allocator::allocator(const char* EASTL_NAME(pName))
    {
#if LE_DEBUG
        mpName = pName;
#endif
    }

    allocator::allocator(const allocator& EASTL_NAME(alloc))
    {
#if LE_DEBUG
        mpName = EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
    }

    allocator::allocator(const allocator&, const char* EASTL_NAME(pName))
    {
#if LE_DEBUG
        mpName = pName;
#endif
    }

    allocator& allocator::operator=(const allocator& EASTL_NAME(alloc))
    {
        return *this;
    }

    const char* allocator::get_name() const
    {
        return "Lumina";
    }

    void allocator::set_name(const char* EASTL_NAME(pName))
    {
        // Implement set_name logic
    }

    void* allocator::allocate(size_t n, int flags)
    {
        return malloc(n);
    }

    void* allocator::allocate(size_t n, size_t alignment, size_t offset, int flags)
    {
        return malloc(n);
    }

    void allocator::deallocate(void* p, size_t)
    {
        free(p);
    }

    bool operator==( allocator const&, allocator const& ) { return true; }
    bool operator!=( allocator const&, allocator const& ) { return false; }
}

DECLARE_MODULE_ALLOCATOR_OVERRIDES();