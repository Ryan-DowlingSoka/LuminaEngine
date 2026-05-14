#pragma once

#include "Platform/GenericPlatform.h"

namespace Lumina::CrashHandler
{
    // Registers an unhandled-exception filter, std::terminate handler, and
    // signal handlers (SIGABRT). On crash: flushes the log, writes a minidump
    // next to the executable, and pops a modal with the dump path. Call once,
    // as early in main as possible.
    RUNTIME_API void Install();

    RUNTIME_API void Shutdown();
}
