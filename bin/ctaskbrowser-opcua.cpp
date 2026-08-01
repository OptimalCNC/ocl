#include <rtt/opcua/task_context_proxy.hpp>
#include <rtt/os/main.h>
#include <rtt/plugin/PluginLoader.hpp>
#include <rtt/rtt-config.h>

#include <taskbrowser/TaskBrowser.hpp>

#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "installpath.hpp"

#define ORO_xstr(s) ORO_str(s)
#define ORO_str(s) #s

namespace {

void printUsage(const char *program) {
  std::cerr << "Usage: " << program
            << " <opc.tcp://host:port/path> <ComponentName>" << std::endl;
}

} // namespace

int ORO_main(int argc, char **argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::cout << "OROCOS Toolchain version '" ORO_xstr(RTT_VERSION) "'"
              << std::endl;
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
    printUsage(argv[0]);
    return 0;
  }
  if (argc != 3) {
    printUsage(argv[0]);
    return -1;
  }

  try {
    RTT::plugin::PluginLoader::Instance()->loadTypekits(ocl_install_path);

    std::string error;
    std::unique_ptr<RTT::opcua::TaskContextProxy> proxy =
        RTT::opcua::TaskContextProxy::create(argv[1], argv[2], {}, &error);
    if (!proxy) {
      std::cerr << "Unable to connect to OPC UA component '" << argv[2]
                << "': " << error << std::endl;
      return -1;
    }

    OCL::TaskBrowser browser(proxy.get());
    browser.loop();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OPC UA TaskBrowser failed: " << error.what() << std::endl;
  } catch (...) {
    std::cerr << "OPC UA TaskBrowser failed with an unknown exception."
              << std::endl;
  }
  return -1;
}
