#include "pch.h"
#include "Log.h"

#include "Core/Console/ConsoleVariable.h"

PRAGMA_DISABLE_ALL_WARNINGS
#include <filesystem>
#include "Sinks/ConsoleSink.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include "spdlog/sinks/ringbuffer_sink.h"
PRAGMA_ENABLE_ALL_WARNINGS

namespace Lumina::Logging
{

	static std::shared_ptr<spdlog::logger> Logger;
	
	
	bool IsInitialized()
	{
		return Logger != nullptr;
	}

	void Init()
	{
		spdlog::set_pattern("%^[%T] %n: %v%$");
		Logger = spdlog::stdout_color_mt("Lumina");
		Logger->sinks().push_back(std::make_shared<FConsoleSink>(GetConsoleLogQueue()));
		Logger->set_level(spdlog::level::trace);
	
		LOG_TRACE("------- Log Initialized -------");
	}

	void Shutdown()
	{
		LOG_TRACE("------- Log Shutdown -------");
		spdlog::shutdown();
		Logger = nullptr;
	}

	void ClearLogQueue()
	{
		GetConsoleLogQueue().clear();
	}

	FLogQueue& GetConsoleLogQueue()
	{
		static FLogQueue Logs(300);
		return Logs;
	}

	const std::shared_ptr<spdlog::logger>& GetLogger()
	{
		return Logger;
	}
	
	const std::shared_ptr<spdlog::sinks::sink>& GetSink()
	{
		return Logger->sinks().front();
	}
	
}
