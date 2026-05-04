#pragma once

#include "Shader.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina
{
    // SPIR-V cache living under /Engine/Intermediate/ShaderCache (editor-writable;
    // bundled into .pak by the cooker so packaged builds skip Slang entirely).
    namespace FShaderCache
    {
        // Bump when the .lsc binary layout or compile pipeline changes in a way
        // that invalidates older entries (e.g. Slang upgrade, header layout).
        constexpr uint32 SHADER_CACHE_VERSION = 1;

        constexpr const char* CACHE_DIR = "/Intermediates/ShaderCache";

        // Hash of the main .slang source plus every transitively-imported/included
        // .slang file we can resolve, plus the (sorted) define list.
        // Returns 0 if the main source can't be read.
        uint64 ComputeSourceSetHash(FStringView ShaderVirtualPath, const TVector<FString>& Defines);

        // Stable cache filename for (shader path + defines), independent of disk layout.
        FString CachePathFor(FStringView ShaderVirtualPath, const TVector<FString>& Defines);

        // Hit only if the file exists, magic/version match, and SourceHash equals
        // the stored one. SourceHash == 0 disables the source-hash check (used in
        // packaged builds where source files aren't shipped — trust the cache).
        bool TryLoad(FStringView ShaderVirtualPath, const TVector<FString>& Defines, uint64 SourceHash, FShaderHeader& OutHeader);

        // Same as TryLoad but loads directly from a known cache file path.
        // SourceHash == 0 disables the check.
        bool TryLoadByCachePath(FStringView CacheVirtualPath, uint64 SourceHash, FShaderHeader& OutHeader);

        // Atomic write under CACHE_DIR. Creates the directory if missing.
        bool Save(FStringView ShaderVirtualPath, const TVector<FString>& Defines, uint64 SourceHash, const FShaderHeader& Header);
    }
}
