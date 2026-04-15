#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Platform/GenericPlatform.h"
#include "Platform/Platform.h"


namespace Lumina::Platform
{
    struct FFileDialogResult
    {
        bool    bSuccess = false;
        FString FilePath;
    };
    
    RUNTIME_API void* GetDLLHandle(const TCHAR* Filename);
    RUNTIME_API bool FreeDLLHandle(void* DLLHandle);
    RUNTIME_API void* GetDLLExport(void* DLLHandle, const char* ProcName);
    RUNTIME_API void AddDLLDirectory(const FString& Directory);

    RUNTIME_API void PushDLLDirectory(const TCHAR* Directory);
    RUNTIME_API void PopDLLDirectory();

    RUNTIME_API uint32 GetCurrentCoreNumber();
    RUNTIME_API FString GetCurrentProcessPath();

	RUNTIME_API int LaunchProcess(const TCHAR* URL, const TCHAR* Params = nullptr, bool bLaunchDetached = true);
    RUNTIME_API void LaunchURL(const TCHAR* URL);

    RUNTIME_API const TCHAR* ExecutableName(bool bRemoveExtension = true);

    RUNTIME_API size_t GetProcessMemoryUsageBytes();
    RUNTIME_API size_t GetProcessMemoryUsageMegaBytes();
    
    RUNTIME_API const TCHAR* BaseDir();
    
    RUNTIME_API FVoidFuncPtr LumGetProcAddress(void* Handle, const char* Procedure);
    RUNTIME_API void* LoadLibraryWithSearchPaths(const FString& Filename, const TVector<FString>& SearchPaths);

    RUNTIME_API bool OpenFileDialogue(FFixedString& OutFile, const char* Title = "Open File", const char* Filter = nullptr, const char* InitialDir = nullptr);
    

    
    
    template<typename TCall>
    requires(std::is_pointer_v<TCall> && std::is_function_v<std::remove_pointer_t<TCall>>)
    TCall LumGetProcAddress(void* Handle, const char* Procedure)
    {
        return reinterpret_cast<TCall>(LumGetProcAddress(Handle, Procedure));
    }
}
