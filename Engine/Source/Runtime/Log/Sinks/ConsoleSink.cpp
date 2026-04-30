#include "pch.h"
#include "ConsoleSink.h"

#include "Core/Templates/LuminaTemplate.h"

namespace Lumina
{
    FConsoleSink::FConsoleSink(Logging::FLogQueue& InOutputMessages)
        : OutputMessages(InOutputMessages)
    {
    }

    void FConsoleSink::sink_it_(const spdlog::details::log_msg& msg)
    {
        FConsoleMessage Message;

        auto ZonedTime = std::chrono::zoned_time{std::chrono::current_zone(), msg.time};
        auto Seconds = std::chrono::floor<std::chrono::seconds>(ZonedTime.get_local_time());
        
        std::format_to(std::back_inserter(Message.Time), "{:%H:%M:%S}", Seconds);
        Message.LoggerName  = FStringView(msg.logger_name.data(), msg.logger_name.size());
        Message.Message     = FFixedString(msg.payload.data(), msg.payload.size());
        Message.Level       = msg.level;

        OutputMessages.push_back(Move(Message));
    }

    void FConsoleSink::flush_()
    {
        // No-op: the ring buffer is the visible console history. spdlog calls flush_()
        // on warn/err via flush_on(), which previously wiped the panel mid-session.
    }
}
