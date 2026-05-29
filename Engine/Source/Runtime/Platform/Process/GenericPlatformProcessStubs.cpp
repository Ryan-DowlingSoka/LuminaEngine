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
    static void WarnOnce(const char* What)
    {
        static bool bWarned = false;
        if (!bWarned)
        {
            bWarned = true;
            LOG_WARN("{0}: no implementation on this platform; ignoring.", What);
        }
    }

    void ShowFileInExplorer(const TCHAR* /*Path*/)
    {
        WarnOnce("Platform::ShowFileInExplorer");
    }

    void ShowFolderInExplorer(const TCHAR* /*Directory*/)
    {
        WarnOnce("Platform::ShowFolderInExplorer");
    }

    void OpenTerminalAt(const TCHAR* /*Directory*/)
    {
        WarnOnce("Platform::OpenTerminalAt");
    }
}

#endif
