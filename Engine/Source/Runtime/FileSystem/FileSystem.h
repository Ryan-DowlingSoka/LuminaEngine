#pragma once

#include "FileInfo.h"
#include "IFileSystem.h"
#include "MemoryFileSystem.h"
#include "NativeFileSystem.h"
#include "Containers/Function.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Core/Templates/LuminaTemplate.h"
#include "Memory/SmartPtr.h"

namespace Lumina::VFS
{
    namespace Detail
    {
        RUNTIME_API IFileSystem& AddFileSystemImpl(const FFixedString& Alias, TUniquePtr<IFileSystem> System);
    }

    template<typename T, typename... TArgs>
    requires std::derived_from<T, IFileSystem> && std::constructible_from<T, FFixedString, TArgs...>
    T& Mount(const FFixedString& Alias, TArgs&&... Args)
    {
        TUniquePtr<T> Owned = MakeUnique<T>(Alias, Forward<TArgs>(Args)...);
        T& Result = *Owned;
        Detail::AddFileSystemImpl(Alias, TUniquePtr<IFileSystem>(Owned.release()));
        return Result;
    }

    RUNTIME_API void DirectoryIterator(FStringView Path, const TFunction<void(const FFileInfo&)>& Callback);
    RUNTIME_API void RecursiveDirectoryIterator(FStringView Path, const TFunction<void(const FFileInfo&)>& Callback);

    RUNTIME_API bool IsEmpty(FStringView Directory);
    RUNTIME_API FStringView RemoveExtension(FStringView Path);
    RUNTIME_API bool Rename(FStringView Old, FStringView New);
    RUNTIME_API FFixedString MakeUniqueFilePath(FStringView BasePath);
    RUNTIME_API FStringView Extension(FStringView Path);
    RUNTIME_API FStringView FileName(FStringView Path, bool bRemoveExtension = false);
    RUNTIME_API bool Remove(FStringView Path);
    RUNTIME_API size_t Size(FStringView Path);
    RUNTIME_API bool RemoveAll(FStringView Path);
    RUNTIME_API FFixedString ResolvePath(FStringView Path);
    RUNTIME_API bool DoesAliasExists(const FName& Alias);
    RUNTIME_API bool CreateDir(FStringView Path);
    RUNTIME_API bool IsUnderDirectory(FStringView Parent, FStringView Path);
    RUNTIME_API bool IsDirectory(FStringView Path);
    RUNTIME_API bool IsLuaAsset(FStringView Path);
    RUNTIME_API bool IsLuminaAsset(FStringView Path);
    RUNTIME_API FStringView Parent(FStringView Path, bool bRemoveTrailingSlash = false);

    RUNTIME_API bool ReadFile(TVector<uint8>& Result, FStringView Path);
    RUNTIME_API bool ReadFile(FString& OutString, FStringView Path);
    RUNTIME_API bool WriteFile(FStringView Path, FStringView Data);
    RUNTIME_API bool WriteFile(FStringView Path, TSpan<const uint8> Data);
    RUNTIME_API bool AtomicWriteFile(FStringView Path, TSpan<const uint8> Data);

    RUNTIME_API void PlatformOpen(FStringView Path);

    RUNTIME_API bool Exists(FStringView Path);

    RUNTIME_API bool HasExtension(FStringView Path, FStringView Ext);

    /**
     * Best-effort canonical conversion from any path the editor might hand us
     * back (absolute Windows path from a file dialog, mixed slashes, an
     * already-virtual path) into the VFS form ("/Game/Content/Foo.lasset")
     * that the asset registry / LoadObject expect.
     *
     * Strategy:
     *   1. If the input already starts with '/' it's treated as VFS already,
     *      just normalized for slashes and returned.
     *   2. Otherwise the input is matched against the BasePath of every native
     *      mount; the first hit is rewritten as `<alias>/<tail>`.
     *   3. If nothing matches, returns the input verbatim (with slashes
     *      normalized) — caller decides whether to treat that as failure.
     */
    RUNTIME_API FFixedString ResolveToVirtualPath(FStringView InputPath);
}
