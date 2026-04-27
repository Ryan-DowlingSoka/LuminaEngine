#include "pch.h"
#include "Log.h"

#include "Core/Console/ConsoleVariable.h"

PRAGMA_DISABLE_ALL_WARNINGS
#include <filesystem>
#include "Sinks/ConsoleSink.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "spdlog/sinks/ringbuffer_sink.h"
PRAGMA_ENABLE_ALL_WARNINGS

#include "Platform/Process/PlatformProcess.h"

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

		// File sink. Cooked builds run as WindowedApp with no console, so the
		// stdout sink is a black hole — without a file we have zero visibility
		// into init crashes, asset registry stalls, or shader compile failures.
		// Lives next to the exe so users can attach it to bug reports.
		try
		{
			std::filesystem::path ExePath(Platform::BaseDir());
			std::filesystem::path LogPath = ExePath.parent_path() / "Lumina.log";

			auto FileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LogPath.string(), /*truncate=*/true);
			FileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
			Logger->sinks().push_back(FileSink);
		}
		catch (const std::exception&)
		{
			// File-sink failure is non-fatal — better to keep running with
			// console-only logging than to crash the engine.
		}

		Logger->set_level(spdlog::level::trace);
		Logger->flush_on(spdlog::level::warn);

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
