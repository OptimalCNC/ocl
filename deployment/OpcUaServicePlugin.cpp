#include "OpcUaDeploymentService.hpp"

#include <rtt/plugin/ServicePlugin.hpp>

extern "C" {

RTT_EXPORT bool loadRTTPlugin(RTT::TaskContext *owner) {
  if (!owner) {
    return true;
  }
  auto *deployer = dynamic_cast<OCL::DeploymentComponent *>(owner);
  if (!deployer) {
    return false;
  }
  try {
    return static_cast<bool>(OCL::OpcUaDeploymentService::attach(*deployer));
  } catch (...) {
    return false;
  }
}

RTT_EXPORT RTT::Service::shared_ptr createService() { return {}; }
RTT_EXPORT std::string getRTTPluginName() { return "opcua"; }
RTT_EXPORT std::string getRTTTargetName() { return OROCOS_TARGET_NAME; }
}
