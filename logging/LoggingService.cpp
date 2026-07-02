#include "logging/LoggingService.hpp"
#include "ocl/Component.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

using namespace RTT;
using namespace std;

namespace OCL {
namespace logging {
namespace {

std::string upperCopy(const std::string& value)
{
    std::string copy(value);
    std::transform(copy.begin(), copy.end(), copy.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return copy;
}

Logger::LogLevel normalizePriority(int priority)
{
    if (priority <= static_cast<int>(Logger::Never))
        return Logger::Never;
    if (priority == static_cast<int>(Logger::Fatal))
        return Logger::Fatal;
    if (priority == static_cast<int>(Logger::Critical))
        return Logger::Critical;
    if (priority == static_cast<int>(Logger::Error))
        return Logger::Error;
    if (priority == static_cast<int>(Logger::Warning))
        return Logger::Warning;
    if (priority == static_cast<int>(Logger::Info))
        return Logger::Info;
    if (priority == static_cast<int>(Logger::Debug))
        return Logger::Debug;
    return Logger::RealTime;
}

const char* priorityName(Logger::LogLevel priority)
{
    switch (priority) {
    case Logger::Never:
        return "NEVER";
    case Logger::Fatal:
        return "FATAL";
    case Logger::Critical:
        return "CRITICAL";
    case Logger::Error:
        return "ERROR";
    case Logger::Warning:
        return "WARNING";
    case Logger::Info:
        return "INFO";
    case Logger::Debug:
        return "DEBUG";
    case Logger::RealTime:
        return "REALTIME";
    }
    return "UNKNOWN";
}

bool parsePriorityName(const std::string& raw, Logger::LogLevel& priority)
{
    const std::string name = upperCopy(raw);
    if (name == "NEVER" || name == "OFF") {
        priority = Logger::Never;
        return true;
    }
    if (name == "EMERG" || name == "FATAL" || name == "ALERT") {
        priority = Logger::Fatal;
        return true;
    }
    if (name == "CRIT" || name == "CRITICAL") {
        priority = Logger::Critical;
        return true;
    }
    if (name == "ERROR") {
        priority = Logger::Error;
        return true;
    }
    if (name == "WARN" || name == "WARNING") {
        priority = Logger::Warning;
        return true;
    }
    if (name == "NOTICE" || name == "INFO") {
        priority = Logger::Info;
        return true;
    }
    if (name == "DEBUG" || name == "NOTSET") {
        priority = Logger::Debug;
        return true;
    }
    if (name == "REALTIME") {
        priority = Logger::RealTime;
        return true;
    }

    char* end = 0;
    const long value = std::strtol(name.c_str(), &end, 10);
    if (end != name.c_str() && *end == '\0') {
        priority = normalizePriority(static_cast<int>(value));
        return true;
    }

    return false;
}

bool hasProperties(const PropertyBag& bag)
{
    return !bag.getProperties().empty();
}

}

LoggingService::LoggingService(std::string name) :
        RTT::TaskContext(name),
        level_EMERG_attr ("EMERG",  Logger::Fatal ),
        level_FATAL_attr ("FATAL",  Logger::Fatal ),
        level_ALERT_attr ("ALERT",  Logger::Fatal ),
        level_CRIT_attr  ("CRIT",   Logger::Critical ),
        level_ERROR_attr ("ERROR",  Logger::Error ),
        level_WARN_attr  ("WARN",   Logger::Warning ),
        level_NOTICE_attr("NOTICE", Logger::Info ),
        level_INFO_attr  ("INFO",   Logger::Info ),
        level_DEBUG_attr ("DEBUG",  Logger::Debug ),
        level_NOTSET_attr("NOTSET", Logger::Debug ),
        setCategoryPriority_mtd("setCategoryPriority", &LoggingService::setCategoryPriority, this),
        getCategoryPriorityName_mtd("getCategoryPriorityName", &LoggingService::getCategoryPriorityName, this),
        setLogLevel_mtd("setLogLevel", &LoggingService::setLogLevel, this),
        getLogLevelName_mtd("getLogLevelName", &LoggingService::getLogLevelName, this),
        drainLog_mtd("drainLog", &LoggingService::drainLog, this),
        droppedLogCount_mtd("droppedLogCount", &LoggingService::droppedLogCount, this),
        getLogLine_mtd("getLogLine", &LoggingService::getLogLine, this),
        levels_prop("Levels","A PropertyBag defining the global RTT logger level."),
        additivity_prop("Additivity","Compatibility property. Category additivity is ignored by the RTT logger backend."),
        appenders_prop("Appenders","Compatibility property. Appenders are ignored by the RTT logger backend."),
        logCategories_mtd("logCategories", &LoggingService::logCategories, this)
{
    this->properties()->addProperty( levels_prop );
    this->properties()->addProperty( additivity_prop );
    this->properties()->addProperty( appenders_prop );
    this->provides()->addAttribute(level_EMERG_attr);
    this->provides()->addAttribute(level_FATAL_attr);
    this->provides()->addAttribute(level_ALERT_attr);
    this->provides()->addAttribute(level_CRIT_attr);
    this->provides()->addAttribute(level_ERROR_attr);
    this->provides()->addAttribute(level_WARN_attr);
    this->provides()->addAttribute(level_NOTICE_attr);
    this->provides()->addAttribute(level_INFO_attr);
    this->provides()->addAttribute(level_DEBUG_attr);
    this->provides()->addAttribute(level_NOTSET_attr);
    this->provides()->addOperation( setCategoryPriority_mtd ).doc("Set the global RTT logger level").arg("name", "Compatibility category name").arg("p", "Priority to set");
    this->provides()->addOperation( getCategoryPriorityName_mtd ).doc("Get the global RTT logger level name").arg("name", "Compatibility category name");
    this->provides()->addOperation( setLogLevel_mtd ).doc("Set the global RTT logger level").arg("p", "Priority to set");
    this->provides()->addOperation( getLogLevelName_mtd ).doc("Get the global RTT logger level name");
    this->provides()->addOperation( drainLog_mtd ).doc("Drain queued RTT log messages");
    this->provides()->addOperation( droppedLogCount_mtd ).doc("Get number of dropped RTT log messages");
    this->provides()->addOperation( getLogLine_mtd ).doc("Pop one historical RTT log line");
    this->provides()->addOperation( logCategories_mtd ).doc("Log RTT logger backend state");
}

LoggingService::~LoggingService()
{
}

bool LoggingService::configureHook()
{
    Logger::log().logf(Logger::Debug, "LoggingService::configureHook",
                       "Configuring RTT logger service");

    bool ok = true;
    PropertyBag bag = levels_prop.value();
    PropertyBag::const_iterator it;
    for (it = bag.getProperties().begin(); it != bag.getProperties().end(); ++it) {
        Property<std::string>* level = dynamic_cast<Property<std::string>* >( *it );
        if (!level) {
            Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                               "Expected Property '%s' to be of type string.",
                               (*it)->getName().c_str());
            ok = false;
            continue;
        }

        Logger::LogLevel priority = Logger::Info;
        if (!parsePriorityName(level->value(), priority)) {
            Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                               "Bad level name: %s", level->value().c_str());
            ok = false;
            continue;
        }

        Logger::log().setLogLevel(priority);
        Logger::log().logf(Logger::Info, "LoggingService::configureHook",
                           "RTT logger level set to '%s' from entry '%s'",
                           priorityName(priority), level->getName().c_str());
    }

    if (hasProperties(additivity_prop.value())) {
        Logger::log().logf(Logger::Warning, "LoggingService::configureHook",
                           "Additivity configuration is ignored by the RTT logger backend.");
    }

    if (hasProperties(appenders_prop.value())) {
        Logger::log().logf(Logger::Warning, "LoggingService::configureHook",
                           "Appender configuration is ignored by the RTT logger backend.");
    }

    return ok;
}

bool LoggingService::setCategoryPriority(const std::string& name,
                                         const int          priority)
{
    const bool ok = setLogLevel(priority);
    if (ok) {
        Logger::log().logf(Logger::Info, "LoggingService::setCategoryPriority",
                           "RTT logger level set to '%s' for compatibility category '%s'",
                           getLogLevelName().c_str(), name.c_str());
    }
    return ok;
}

std::string LoggingService::getCategoryPriorityName(const std::string& name)
{
    const std::string level = getLogLevelName();
    Logger::log().logf(Logger::Info, "LoggingService::getCategoryPriorityName",
                       "Compatibility category '%s' has RTT logger level '%s'",
                       name.c_str(), level.c_str());
    return level;
}

bool LoggingService::setLogLevel(const int priority)
{
    Logger::log().setLogLevel(normalizePriority(priority));
    return true;
}

std::string LoggingService::getLogLevelName()
{
    return priorityName(Logger::log().getLogLevel());
}

int LoggingService::drainLog()
{
    return Logger::log().drainLog();
}

int LoggingService::droppedLogCount()
{
    const std::size_t count = Logger::log().droppedLogCount();
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(count);
}

std::string LoggingService::getLogLine()
{
    Logger::log().drainLog();
    return Logger::log().getLogLine();
}

void LoggingService::logCategories()
{
    Logger::log().logf(Logger::Info, "LoggingService::logCategories",
                       "RTT logger backend active. Global level=%s, dropped=%d",
                       getLogLevelName().c_str(), droppedLogCount());
}

}
}

ORO_CREATE_COMPONENT_TYPE();
ORO_LIST_COMPONENT_TYPE(OCL::logging::LoggingService);
