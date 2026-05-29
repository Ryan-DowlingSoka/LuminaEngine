// Single canonical EASTL allocator binding for the engine.
//
// EASTL's `allocator` class is non-template, non-inline. Its method bodies
// and the `eastl::GDefaultAllocator` global must be defined exactly once per
// linked image (DLL / EXE). Every consumer (Runtime, Editor, Lumina.exe,
// game DLLs) used to ship its own boilerplate `Lumina_eastl.cpp` next to
// its own source. Now this single file is auto-added to every consumer's
// compile set by the build system ([BuildScripts/Module.lua] for engine
// modules, [BuildScripts/GameProject.lua] for game projects), so each
// image still gets its own correctly-decorated copy without the user
// having to ship boilerplate.
//
// Both Malloc and Free route through Runtime-DLL-exported Lumina::Memory
// functions, so allocations are pool-coherent across DLL boundaries — a
// pointer Malloc'd in one DLL can be Free'd in another safely.

#include <EASTL/internal/config.h>
#include <EASTL/allocator.h>
#include <EASTL/string.h>

#include "Core/Assertions/Assert.h"
#include "Memory/Memory.h"


namespace eastl
{
    allocator GDefaultAllocator;

    //-------------------------------------------------------------------------

    allocator* GetDefaultAllocator()
    {
        return &GDefaultAllocator;
    }

    allocator* SetDefaultAllocator(allocator*)
    {
        return &GDefaultAllocator;
    }

    //-------------------------------------------------------------------------

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
    }

    void* allocator::allocate(size_t n, int /*flags*/)
    {
        return Lumina::Memory::Malloc(n, EASTL_ALLOCATOR_MIN_ALIGNMENT);
    }

    void* allocator::allocate(size_t n, size_t alignment, size_t /*offset*/, int /*flags*/)
    {
        return Lumina::Memory::Malloc(n, alignment);
    }

    void allocator::deallocate(void* p, size_t)
    {
        Lumina::Memory::Free(p);
    }

    bool operator==(allocator const&, allocator const&) { return true; }
    bool operator!=(allocator const&, allocator const&) { return false; }
}


// EASTL's debug-mode operator new[] overloads.
void* operator new[](size_t size, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    return Lumina::Memory::Malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    EASTL_ASSERT(alignment <= 8);
    return Lumina::Memory::Malloc(size, alignment);
}

namespace eastl
{
    void AssertionFailure(const char* Expression)
    {
        PANIC("{0}", Expression);
    }
}

int Vsnprintf8(char* dest, size_t n, const char* fmt, char* argPtr)
{
    va_list args = *reinterpret_cast<va_list*>(&argPtr);
    return vsnprintf(dest, n, fmt, args);
}
