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

#define LOG_CRITICAL(...)	::Lumina::Logging::GetLogger()->critical(__VA_ARGS__)
#define LOG_ERROR(...)		::Lumina::Logging::GetLogger()->error(__VA_ARGS__)
#define LOG_WARN(...)		::Lumina::Logging::GetLogger()->warn(__VA_ARGS__)
#define LOG_TRACE(...)		::Lumina::Logging::GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)		::Lumina::Logging::GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)		::Lumina::Logging::GetLogger()->info(__VA_ARGS__)
