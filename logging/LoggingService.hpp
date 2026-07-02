#ifndef LOGGINGSERVICE_HPP
#define LOGGINGSERVICE_HPP 1

#include <rtt/TaskContext.hpp>
#include <rtt/PropertyBag.hpp>
#include <rtt/Operation.hpp>
#include <rtt/Logger.hpp>

namespace OCL {
namespace logging {

/**
 * RTT-backed logging configuration component.
 *
 * RTT owns the bounded logger implementation. This component exposes OCL
 * operations and properties for configuring and draining that logger. The old
 * per-category API names are kept as compatibility aliases for the global RTT
 * logger level.
 */
class LoggingService : public RTT::TaskContext
{
public:
    LoggingService(std::string name);
    virtual ~LoggingService();

    virtual bool configureHook();

    RTT::Attribute<int> level_EMERG_attr;
    RTT::Attribute<int> level_FATAL_attr;
    RTT::Attribute<int> level_ALERT_attr;
    RTT::Attribute<int> level_CRIT_attr;
    RTT::Attribute<int> level_ERROR_attr;
    RTT::Attribute<int> level_WARN_attr;
    RTT::Attribute<int> level_NOTICE_attr;
    RTT::Attribute<int> level_INFO_attr;
    RTT::Attribute<int> level_DEBUG_attr;
    RTT::Attribute<int> level_NOTSET_attr;

    RTT::Operation<bool(std::string,int)> setCategoryPriority_mtd;
    bool setCategoryPriority(const std::string& name, const int priority);

    RTT::Operation<std::string(std::string)> getCategoryPriorityName_mtd;
    std::string getCategoryPriorityName(const std::string& name);

    RTT::Operation<bool(int)> setLogLevel_mtd;
    bool setLogLevel(const int priority);

    RTT::Operation<std::string(void)> getLogLevelName_mtd;
    std::string getLogLevelName();

    RTT::Operation<int(void)> drainLog_mtd;
    int drainLog();

    RTT::Operation<int(void)> droppedLogCount_mtd;
    int droppedLogCount();

    RTT::Operation<std::string(void)> getLogLine_mtd;
    std::string getLogLine();

protected:
    RTT::Property<RTT::PropertyBag> levels_prop;
    RTT::Property<RTT::PropertyBag> additivity_prop;
    RTT::Property<RTT::PropertyBag> appenders_prop;

    RTT::Operation<void(void)> logCategories_mtd;
    void logCategories();
};

}
}

#endif
