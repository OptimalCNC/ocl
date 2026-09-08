// SPDX-License-Identifier: LGPL-2.1-or-later
#include "HttpDeploymentService.hpp"
#include <rtt/plugin/ServicePlugin.hpp>

extern "C" {
RTT_EXPORT bool loadRTTPlugin(RTT::TaskContext *owner) {
  if (!owner) {
    return true;
  } // discovery only
  auto *deployer = dynamic_cast<OCL::DeploymentComponent *>(owner);
  if (!deployer) {
    return false;
  }
  try {
    return static_cast<bool>(OCL::HttpDeploymentService::attach(*deployer));
  } catch (...) {
    return false;
  }
}
RTT_EXPORT RTT::Service::shared_ptr createService() { return {}; }
RTT_EXPORT std::string getRTTPluginName() { return "http"; }
RTT_EXPORT std::string getRTTTargetName() { return OROCOS_TARGET_NAME; }
}
