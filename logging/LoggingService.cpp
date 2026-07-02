#include "logging/LoggingService.hpp"
#include "logging/Category.hpp"
#include "ocl/Component.hpp"

#include <boost/algorithm/string.hpp>
#include <log4cpp/Category.hh>
#include <log4cpp/Priority.hh>
#include <log4cpp/HierarchyMaintainer.hh>

#include <rtt/Logger.hpp>
#include <typeinfo>

using namespace RTT;
using namespace std;

namespace OCL {
namespace logging {

LoggingService::LoggingService(std::string name) :
		RTT::TaskContext(name),
        level_EMERG_attr ("EMERG",  log4cpp::Priority::EMERG ),
        level_FATAL_attr ("FATAL",  log4cpp::Priority::FATAL ),
        level_ALERT_attr ("ALERT",  log4cpp::Priority::ALERT ),
        level_CRIT_attr  ("CRIT",   log4cpp::Priority::CRIT  ),
        level_ERROR_attr ("ERROR",  log4cpp::Priority::ERROR ),
        level_WARN_attr  ("WARN",   log4cpp::Priority::WARN  ),
        level_NOTICE_attr("NOTICE", log4cpp::Priority::NOTICE),
        level_INFO_attr  ("INFO",   log4cpp::Priority::INFO  ),
        level_DEBUG_attr ("DEBUG",  log4cpp::Priority::DEBUG ),
        level_NOTSET_attr("NOTSET", log4cpp::Priority::NOTSET),
        setCategoryPriority_mtd("setCategoryPriority", &LoggingService::setCategoryPriority, this),
        getCategoryPriorityName_mtd("getCategoryPriorityName", &LoggingService::getCategoryPriorityName, this),
        levels_prop("Levels","A PropertyBag defining the level of each category of interest."),
        additivity_prop("Additivity","A PropertyBag defining the additivity of each category of interest."),
        appenders_prop("Appenders","A PropertyBag defining the appenders for each category of interest."),
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
    this->provides()->addOperation( setCategoryPriority_mtd ).doc("Set the priority of category to p").arg("name", "Name of the category").arg("p", "Priority to set");
    this->provides()->addOperation( getCategoryPriorityName_mtd ).doc("Get the priority name of category").arg("name", "Name of the category");
    this->provides()->addOperation( logCategories_mtd ).doc("Log category hierarchy (not realtime!)");
}

LoggingService::~LoggingService()
{
}

bool LoggingService::configureHook()
{
    Logger::log().logf(Logger::Debug, "LoggingService::configureHook",
                       "Configuring LoggingService");

    // set the priority/level for each category

    PropertyBag bag = levels_prop.value();  // an empty bag is ok

    bool ok = true;
    PropertyBag::const_iterator it;
    for (it=bag.getProperties().begin(); it != bag.getProperties().end(); ++it)
    {
        Property<std::string>* category = dynamic_cast<Property<std::string>* >( *it );
        if ( !category )
        {
            Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                               "Expected Property '%s' to be of type string.",
                               (*it)->getName().c_str());
        }
        else 
        {
            std::string categoryName = category->getName();
            std::string levelName    = category->value();

            // "" == categoryName implies the root category.

            // \todo else if level is empty
            
            // log4cpp only takes upper case
            boost::algorithm::to_upper(levelName);
            
            log4cpp::Priority::Value priority = log4cpp::Priority::NOTSET;
            try
            {
                priority = log4cpp::Priority::getPriorityValue(levelName);
                
            }
            catch (std::invalid_argument)
            {
                // \todo more descriptive
                Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                                   "Bad level name: %s", levelName.c_str());
                return false;
            }
            
            Logger::log().logf(Logger::Debug, "LoggingService::configureHook",
                               "Getting category '%s'", categoryName.c_str());
            // will create category if not exists
            log4cpp::Category& category =
                log4cpp::Category::getInstance(categoryName);

            category.setPriority(priority);
            Logger::log().logf(Logger::Info, "LoggingService::configureHook",
                               "Category '%s' has priority '%s'",
                               categoryName.c_str(), levelName.c_str());
        }
    }

    // first clear all existing appenders in order to avoid double connects:
    for(vector<string>::iterator it = active_appenders.begin(); it != active_appenders.end(); ++it) {
        base::PortInterface* port = 0;
        TaskContext* appender	= getPeer(*it);
        if (appender && (port = appender->ports()->getPort("LogPort")) )
            port->disconnect();
    }
    if ( !active_appenders.empty() ) 
        Logger::log().logf(Logger::Warning, "LoggingService::configureHook",
                           "Reconfiguring LoggingService '%s': I've removed all existing Appender connections and will now rebuild them.",
                           getName().c_str());
    active_appenders.clear();

	// set the additivity of each category

    bag = additivity_prop.value();  // an empty bag is ok

    for (it=bag.getProperties().begin(); it != bag.getProperties().end(); ++it)
    {
        Property<bool>* category = dynamic_cast<Property<bool>* >( *it );
        if ( !category )
        {
            Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                               "Expected Property '%s' to be of type boolean.",
                               (*it)->getName().c_str());
        }
        else
        {
            std::string categoryName	= category->getName();
            bool		additivity		= category->value();

            // "" == categoryName implies the root category.

            Logger::log().logf(Logger::Debug, "LoggingService::configureHook",
                               "Getting category '%s'", categoryName.c_str());
            // will create category if not exists
            log4cpp::Category& category =
            log4cpp::Category::getInstance(categoryName);

            category.setAdditivity(additivity);
            Logger::log().logf(Logger::Info, "LoggingService::configureHook",
                               "Category '%s' has additivity '%s'",
                               categoryName.c_str(), additivity ? "on" : "off");
        }
    }

    // create a port for each appender, and associate category/appender

    bag = appenders_prop.value();           // an empty bag is ok

    ok = true;
    for (it=bag.getProperties().begin(); it != bag.getProperties().end(); ++it)
    {
        Property<std::string>* association = dynamic_cast<Property<std::string>* >( *it );
        if ( !association )
        {
            Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                               "Expected Property '%s' to be of type string.",
                               (*it)->getName().c_str());
        }
        // \todo else if name or level are empty
        else 
        {
            std::string categoryName    = association->getName();
            std::string appenderName    = association->value();
            
            // find category - will create category if not exists
            log4cpp::Category& p   = log4cpp::Category::getInstance(categoryName);
            OCL::logging::Category* category =
                dynamic_cast<OCL::logging::Category*>(&p);
            if (0 == category)
            {
                Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                                   "Category '%s' is not an OCL category: type is '%s'",
                                   categoryName.c_str(), typeid(p).name());
                ok = false;
                break;
            }
            
            // find appender
            RTT::TaskContext* appender	= getPeer(appenderName);
            if (appender)
            {
                // connect category port with appender port
                RTT::base::PortInterface* appenderPort = 0;

                appenderPort	= appender->ports()->getPort("LogPort");
                if (appenderPort)
                {
                    // \todo make connection policy configurable (from xml).
                    ConnPolicy cp = ConnPolicy::buffer(100,ConnPolicy::LOCK_FREE,false,false);
                    if ( appenderPort->connectTo( &(category->log_port), cp) )
                    {
                        const std::string level = log4cpp::Priority::getPriorityName(category->getPriority());
                        Logger::log().logf(Logger::Info, "LoggingService::configureHook",
                                           "Category '%s' has appender '%s' with level %s",
                                           categoryName.c_str(), appenderName.c_str(), level.c_str());
                        active_appenders.push_back(appenderName);
                    }
                    else
                    {
                        Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                                           "Failed to connect port to appender '%s'",
                                           appenderName.c_str());
                        ok = false;
                        break;
                    }
                }
                else
                {
                    Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                                       "Failed to find log port in appender");
                    ok = false;
                    break;
                }
            }
            else
            {
                Logger::log().logf(Logger::Error, "LoggingService::configureHook",
                                   "Could not find appender '%s'", appenderName.c_str());
                ok = false;
                break;
            }
        }
    }
    
    return ok;
}

bool LoggingService::setCategoryPriority(const std::string& name,
                                         const int          priority)
{
    bool rc = false;        // prove otherwise
    log4cpp::Category* c = log4cpp::Category::exists(name);
    if (NULL != c)
    {
        try {
            c->setPriority(priority);
            const std::string level = log4cpp::Priority::getPriorityName(priority);
            rc = true;
            Logger::log().logf(Logger::Info, "LoggingService::setCategoryPriority",
                               "Category '%s' set to priority '%s'",
                               name.c_str(), level.c_str());
        } catch (...) {
            Logger::log().logf(Logger::Error, "LoggingService::setCategoryPriority",
                               "Priority value '%d' is not known!", priority);
        }
    }
    else
    {
        Logger::log().logf(Logger::Error, "LoggingService::setCategoryPriority",
                           "Could not find category '%s'", name.c_str());
    }
    return rc;
}

std::string LoggingService::getCategoryPriorityName(const std::string& name)
{
    std::string rc;
    log4cpp::Category* c = log4cpp::Category::exists(name);
    if (NULL != c)
    {
        try {
            rc = log4cpp::Priority::getPriorityName(c->getPriority());
            Logger::log().logf(Logger::Info, "LoggingService::getCategoryPriorityName",
                               "Category '%s' has priority '%s'",
                               name.c_str(), rc.c_str());
        } catch (...) {
            rc = "UNKNOWN PRIORITY";
            Logger::log().logf(Logger::Error, "LoggingService::getCategoryPriorityName",
                               "Category '%s' has unknown priority!", name.c_str());
        }
    }
    else
    {
        rc = "UNKNOWN CATEGORY";
        Logger::log().logf(Logger::Error, "LoggingService::getCategoryPriorityName",
                           "Could not find category '%s'", name.c_str());
    }
    return rc;
}

// NOT realtime
void LoggingService::logCategories()
{
    std::vector<log4cpp::Category*>*            categories =
        log4cpp::Category::getCurrentCategories();
    assert(categories);
    std::vector<log4cpp::Category*>::iterator   iter;
    Logger::log().logf(Logger::Info, "LoggingService::logCategories",
                       "Number categories = %d", static_cast<int>(categories->size()));
    for (iter = categories->begin(); iter != categories->end(); ++iter)
    {
        OCL::logging::Category* c = dynamic_cast<OCL::logging::Category*>(*iter);
        const std::string level = log4cpp::Priority::getPriorityName((*iter)->getPriority());
        const char* type = 0 != c ? "OCL::Category" : "log4cpp::Category";
        const char* additivity = (*iter)->getAdditivity() ? "yes" : "no";
        const char* port = 0 != c ? (c->log_port.connected() ? "connected" : "not connected") : "";
        log4cpp::Category* p = (*iter)->getParent();
        if (0 != c && p)
            Logger::log().logf(Logger::Info, "LoggingService::logCategories",
                               "Category '%s', level=%s, typeid='%s', type really is '%s', additivity=%s, port=%s, parent name='%s'",
                               (*iter)->getName().c_str(), level.c_str(), typeid(*iter).name(), type,
                               additivity, port, p->getName().c_str());
        else if (0 != c)
            Logger::log().logf(Logger::Info, "LoggingService::logCategories",
                               "Category '%s', level=%s, typeid='%s', type really is '%s', additivity=%s, port=%s, No parent",
                               (*iter)->getName().c_str(), level.c_str(), typeid(*iter).name(), type,
                               additivity, port);
        else if (p)
            Logger::log().logf(Logger::Info, "LoggingService::logCategories",
                               "Category '%s', level=%s, typeid='%s', type really is '%s', additivity=%s, parent name='%s'",
                               (*iter)->getName().c_str(), level.c_str(), typeid(*iter).name(), type,
                               additivity, p->getName().c_str());
        else
            Logger::log().logf(Logger::Info, "LoggingService::logCategories",
                               "Category '%s', level=%s, typeid='%s', type really is '%s', additivity=%s, No parent",
                               (*iter)->getName().c_str(), level.c_str(), typeid(*iter).name(), type,
                               additivity);
    }
}
   
// namespaces
}
}

ORO_CREATE_COMPONENT_TYPE();
ORO_LIST_COMPONENT_TYPE(OCL::logging::LoggingService);
