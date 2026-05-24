#include "pch.h"
#include "Log.h"

#include "Core/Console/ConsoleVariable.h"

PRAGMA_DISABLE_ALL_WARNINGS
#include <filesystem>
#include "Sinks/ConsoleSink.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
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

		// Rotating file sink: keeps prior run evidence; WindowedApp builds have no console.
		try
		{
			std::filesystem::path ExePath(Platform::BaseDir());
			std::filesystem::path LogPath = ExePath.parent_path() / "Lumina.log";

			constexpr size_t MaxLogSizeBytes = 16 * 1024 * 1024;
			constexpr size_t MaxLogFiles     = 5;
			auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
				LogPath.string(), MaxLogSizeBytes, MaxLogFiles, /*rotate_on_open=*/true);
			FileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
			Logger->sinks().push_back(FileSink);
		}
		catch (const std::exception&)
		{
			// Non-fatal; fall back to console-only logging.
		}

		// Floor at warn when verbose macros are stripped (Shipping); avoids unfiltered direct calls.
		#if defined(LUMINA_VERBOSE_LOGGING)
		Logger->set_level(spdlog::level::trace);
		#else
		Logger->set_level(spdlog::level::warn);
		#endif
		// Flush eagerly so the last lines before a crash reach disk.
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
