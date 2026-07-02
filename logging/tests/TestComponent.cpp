#include "logging/tests/TestComponent.hpp"
#include <rtt/rt_string.hpp>
#include "logging/Category.hpp"

#include <rtt/Logger.hpp>
#include "ocl/Component.hpp"

#include <log4cpp/HierarchyMaintainer.hh>

namespace OCL {
namespace logging {
namespace test {

static const char* parentCategory = "org.orocos.ocl.logging.tests";

Component::Component(std::string name) :
		RTT::TaskContext(name),
        categoryName(parentCategory + std::string(".") + name),
        logger(dynamic_cast<OCL::logging::Category*>(
                   &log4cpp::Category::getInstance(categoryName)))
{
}

Component::~Component()
{
}

bool Component::startHook()
{
    bool ok = (0 != logger);
    if (!ok)
    {
        RTT::Logger::log().logf(RTT::Logger::Error, "logging tests",
                                "Unable to find existing OCL category '%s'",
                                categoryName.c_str());
    }
    
    return ok;
}
        
void Component::updateHook()
{
	static int i=0;
	RTT::rt_ostringstream	str_a, str_b;
	str_a <<"A:" << getName() << " " << i;
	str_b <<"B:" << getName() << " " << i;

    // OCL logging
    logger->error("ERROR " + RTT::rt_string(str_a.str().c_str()));
    logger->error("ERROR " + RTT::rt_string(str_b.str().c_str()));
    logger->info( "INFO  " + RTT::rt_string(str_a.str().c_str()));
    logger->info( "INFO  " + RTT::rt_string(str_b.str().c_str()));
    logger->debug("DEBUG " + RTT::rt_string(str_a.str().c_str()));
    logger->debug("DEBUG " + RTT::rt_string(str_b.str().c_str()));

    // RTT logging should use RTT::Logger::log().logf(...) when needed here.

    // and trying to use the std::string versions ...
//    logger->error(std::string("Hello")); // COMPILER error - not accessible!
//    logger->debug("DEBUG"); // COMPILER error - not accessible with char*!

	++i;
}

// namespaces
}
}
}

ORO_CREATE_COMPONENT_TYPE();
ORO_LIST_COMPONENT_TYPE(OCL::logging::test::Component);


