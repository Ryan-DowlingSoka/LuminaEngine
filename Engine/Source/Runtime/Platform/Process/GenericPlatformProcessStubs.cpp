// Generic no-op fallbacks for OS-shell-integration helpers declared in
// PlatformProcess.h. Compiled only on platforms that don't have a native
// implementation — on Windows the real bodies live in WindowsPlatformProcess.cpp
// and this file is empty.
//
// The contract documented in PlatformProcess.h is "no-op if no equivalent on
// this OS", so platform-agnostic editor code can call these unconditionally
// without #ifdef walls.

#include "pch.h"

#ifndef _WIN32

#include "Platform/Process/PlatformProcess.h"
#include "Log/Log.h"

namespace Lumina::Platform
{
    // Per-callsite once-flag: each stub gets its own static. A single
    // shared `static bool` would silently swallow every call after the
    // first across the whole trio.
    #define LUMINA_WARN_ONCE(What) \
        do { \
            static bool bWarned_ = false; \
            if (!bWarned_) { bWarned_ = true; \
                LOG_WARN("{0}: no implementation on this platform; ignoring.", What); } \
        } while (0)

    void ShowFileInExplorer(const TCHAR* /*Path*/)
    {
        LUMINA_WARN_ONCE("Platform::ShowFileInExplorer");
    }

    void ShowFolderInExplorer(const TCHAR* /*Directory*/)
    {
        LUMINA_WARN_ONCE("Platform::ShowFolderInExplorer");
    }

    void OpenTerminalAt(const TCHAR* /*Directory*/)
    {
        LUMINA_WARN_ONCE("Platform::OpenTerminalAt");
    }

    bool SetEnvVariable(const FString& Name, const FString& Value)
    {
        if (setenv(Name.c_str(), Value.c_str(), 1) == 0)
        {
            return true;
        }

        LOG_WARN("Failed to set environment variable {}", Name);
        return false;
    }

    bool PersistUserEnvVariable(const FString& /*Name*/, const FString& /*Value*/)
    {
        LUMINA_WARN_ONCE("Platform::PersistUserEnvVariable");
        return false;
    }

    #undef LUMINA_WARN_ONCE
}

#endif
