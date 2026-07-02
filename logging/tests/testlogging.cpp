#include "logging/LoggingService.hpp"

#include <rtt/Logger.hpp>

#include <cassert>

int main(int, char**)
{
    OCL::logging::LoggingService service("LoggingService");

    assert(service.setCategoryPriority("compat.category", RTT::Logger::Info));
    assert(RTT::Logger::log().getLogLevel() == RTT::Logger::Info);
    assert(service.getCategoryPriorityName("compat.category") == "INFO");

    assert(service.setLogLevel(RTT::Logger::Debug));
    assert(RTT::Logger::log().getLogLevel() == RTT::Logger::Debug);
    assert(service.getLogLevelName() == "DEBUG");

    RTT::Logger::log().logf(RTT::Logger::Info, "testlogging",
                            "RTT-backed logging service test");
    service.drainLog();

    return 0;
}
