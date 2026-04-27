#pragma once

#include <mutex>
#include "Core/DisableAllWarnings.h"
#include "Log/Log.h"
PRAGMA_DISABLE_ALL_WARNINGS
#include <spdlog/sinks/base_sink.h>
PRAGMA_ENABLE_ALL_WARNINGS


namespace Lumina
{
    
    class FConsoleSink : public spdlog::sinks::base_sink<std::mutex>
    {
    public:
        FConsoleSink(Logging::FLogQueue& InOutputMessages);

    protected:
        
        void sink_it_(const spdlog::details::log_msg& msg) override;
        void flush_() override;

    private:
        
        Logging::FLogQueue& OutputMessages;
    };
}

