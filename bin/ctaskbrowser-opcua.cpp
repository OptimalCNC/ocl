#include <rtt/deployment/ComponentLoader.hpp>
#include <rtt/opcua/task_context_proxy.hpp>
#include <rtt/opcua/type_protocol.hpp>
#include <rtt/os/main.h>
#include <rtt/plugin/PluginLoader.hpp>
#include <rtt/rtt-config.h>

#include <taskbrowser/TaskBrowser.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "installpath.hpp"

#define ORO_xstr(s) ORO_str(s)
#define ORO_str(s) #s

namespace {

struct CommandLine {
  std::vector<std::string> imports;
  std::string endpoint;
  std::string component;
  bool help{false};
  bool version{false};
};

void printUsage(const char *program) {
  std::cerr << "Usage: " << program
            << " [--import PACKAGE]... <opc.tcp://host:port/path>"
               " <ComponentName>\n"
            << "  --import PACKAGE  Import a local Orocos package before "
               "connecting."
            << std::endl;
}

bool parseCommandLine(int argc, char **argv, CommandLine *result,
                      std::string *error) {
  std::vector<std::string> positional;
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument == "--help") {
      result->help = true;
    } else if (argument == "--version") {
      result->version = true;
    } else if (argument == "--import") {
      if (++index >= argc || argv[index][0] == '\0' || argv[index][0] == '-') {
        *error = "--import requires a package name";
        return false;
      }
      result->imports.emplace_back(argv[index]);
    } else if (argument.rfind("--import=", 0U) == 0U) {
      std::string package = argument.substr(sizeof("--import=") - 1U);
      if (package.empty()) {
        *error = "--import requires a package name";
        return false;
      }
      result->imports.push_back(std::move(package));
    } else if (!argument.empty() && argument.front() == '-') {
      *error = "unknown option: " + argument;
      return false;
    } else {
      positional.push_back(argument);
    }
  }

  if (result->help || result->version) {
    return true;
  }
  if (positional.size() != 2U) {
    *error = "exactly one endpoint and one component name are required";
    return false;
  }
  result->endpoint = std::move(positional[0]);
  result->component = std::move(positional[1]);
  return true;
}

} // namespace

int ORO_main(int argc, char **argv) {
  CommandLine command_line;
  std::string argument_error;
  if (!parseCommandLine(argc, argv, &command_line, &argument_error)) {
    std::cerr << argument_error << std::endl;
    printUsage(argv[0]);
    return -1;
  }
  if (command_line.version) {
    std::cout << "OROCOS Toolchain version '" ORO_xstr(RTT_VERSION) "'"
              << std::endl;
    return 0;
  }
  if (command_line.help) {
    printUsage(argv[0]);
    return 0;
  }

  try {
    RTT::plugin::PluginLoader::Instance()->loadTypekits(ocl_install_path);
    for (const std::string &package : command_line.imports) {
      if (!RTT::ComponentLoader::Instance()->import(package, "")) {
        std::cerr << "Unable to import local Orocos package '" << package
                  << "'." << std::endl;
        return -1;
      }
    }
    std::string error;
    if (!RTT::opcua::registerCanonicalTypeProtocols(&error)) {
      std::cerr << "Unable to register canonical OPC UA types: " << error
                << std::endl;
      return -1;
    }

    std::unique_ptr<RTT::opcua::TaskContextProxy> proxy =
        RTT::opcua::TaskContextProxy::create(
            command_line.endpoint, command_line.component, {}, &error);
    if (!proxy) {
      std::cerr << "Unable to connect to OPC UA component '"
                << command_line.component << "': " << error << std::endl;
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
