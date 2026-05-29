#pragma once

#include "Containers/Array.h"
#include "Core/DisableAllWarnings.h"


PRAGMA_DISABLE_ALL_WARNINGS
#include <spdlog/spdlog.h>
PRAGMA_ENABLE_ALL_WARNINGS
#include "LogMessage.h"


namespace Lumina::Logging
{
	RUNTIME_API bool IsInitialized();
	RUNTIME_API void Init();
	RUNTIME_API void Shutdown();

	using FLogQueue = TRingBuffer<FConsoleMessage>;

	RUNTIME_API void ClearLogQueue();
	RUNTIME_API FLogQueue& GetConsoleLogQueue();
	RUNTIME_API const std::shared_ptr<spdlog::logger>& GetLogger();
	RUNTIME_API const std::shared_ptr<spdlog::sinks::sink>& GetSink();
	
}

// WARN/ERROR/CRITICAL always compile in — they carry crash diagnostics you
// want even in a shipped build.
#define LOG_CRITICAL(...)	::Lumina::Logging::GetLogger()->critical(__VA_ARGS__)
#define LOG_ERROR(...)		::Lumina::Logging::GetLogger()->error(__VA_ARGS__)
#define LOG_WARN(...)		::Lumina::Logging::GetLogger()->warn(__VA_ARGS__)

// DISPLAY also always compiles in — info-severity, but reserved for one-shot
// boot/system milestones we still want in Shipping so post-mortem debugging
// of a packaged game (black screen, missing map, plugin not loaded, etc.) has
// breadcrumbs. NOT a general-purpose info channel — keep these rare; use
// LOG_INFO for everyday verbose status.
#define LOG_DISPLAY(...)	::Lumina::Logging::GetLogger()->info(__VA_ARGS__)

// TRACE/DEBUG/INFO are verbose levels gated by the VerboseLogging build option
// (LUMINA_VERBOSE_LOGGING, see BuildConfig.lua). When it's off, these expand to
// nothing so their format strings are dropped from the binary and the per-call
// disk I/O disappears. Defaults: on in Debug/Development, off in Shipping.
#if defined(LUMINA_VERBOSE_LOGGING)
	#define LOG_TRACE(...)	::Lumina::Logging::GetLogger()->trace(__VA_ARGS__)
	#define LOG_DEBUG(...)	::Lumina::Logging::GetLogger()->debug(__VA_ARGS__)
	#define LOG_INFO(...)	::Lumina::Logging::GetLogger()->info(__VA_ARGS__)
#else
	#define LOG_TRACE(...)	((void)0)
	#define LOG_DEBUG(...)	((void)0)
	#define LOG_INFO(...)	((void)0)
#endif
