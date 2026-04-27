#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
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

    /**
     * Synchronously runs a command line. The child process gets its own
     * console window so the user can watch output (handy for MSBuild). This
     * call blocks until the process exits and returns its exit code; -1
     * means the process failed to spawn.
     */
    RUNTIME_API int RunProcessAndWait(const TCHAR* Executable, const TCHAR* Params, const TCHAR* WorkingDirectory = nullptr);

    /**
     * Like RunProcessAndWait, but no console pops up — the child's stdout
     * AND stderr are merged and streamed back through LineCallback, one
     * complete line per call. Useful when the editor needs to display a
     * subprocess's output inline (cooker, build pipeline).
     *
     * The callback runs on the calling thread between line boundaries, so
     * it's safe to push into UI state without locks. Returns the exit code,
     * or -1 if the process failed to spawn.
     */
    RUNTIME_API int RunProcessAndWaitCapture(const TCHAR* Executable, const TCHAR* Params, const TCHAR* WorkingDirectory, const TFunction<void(FStringView)>& LineCallback);

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
