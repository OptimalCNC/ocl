#ifndef OCL_OPCUADEPLOYMENTCOMPONENT_HPP
#define OCL_OPCUADEPLOYMENTCOMPONENT_HPP

#include "DeploymentComponent.hpp"

#include <rtt/opcua/object_model.hpp>
#include <rtt/opcua/server_options.hpp>
#include <rtt/opcua/task_context_proxy.hpp>

#include <memory>
#include <string>
#include <vector>

namespace OCL {

struct OCL_API OpcUaDeploymentOptions {
  RTT::opcua::ServerOptions server;
  RTT::opcua::ObjectModelOptions object_model;
  RTT::opcua::TaskContextProxyOptions proxy;
};

class OCL_API OpcUaDeploymentComponent : public DeploymentComponent {
public:
  explicit OpcUaDeploymentComponent(const std::string &name = "Deployer",
                                    const std::string &site_file = "",
                                    OpcUaDeploymentOptions options = {});
  ~OpcUaDeploymentComponent() override;

  OpcUaDeploymentComponent(const OpcUaDeploymentComponent &) = delete;
  OpcUaDeploymentComponent &
  operator=(const OpcUaDeploymentComponent &) = delete;

  bool startOpcUa();
  bool opcUaIsRunning() const;
  std::string opcUaEndpointUrl() const;
  std::string opcUaLastError() const;
  bool publishComponent(const std::string &component_name);
  bool publishComponentSelected(
      const std::string &component_name,
      const std::vector<std::string> &selectors);
  std::vector<std::string>
  publicationDiagnostics(const std::string &component_name) const;
  std::vector<std::string>
  unsupportedResources(const std::string &component_name) const;

  bool connectRemote(const std::string &endpoint_url,
                     const std::string &component_name,
                     const std::string &peer_name);
  bool disconnectRemote(const std::string &peer_name);
  bool synchronizeRemote(const std::string &peer_name);

protected:
  bool componentLoaded(RTT::TaskContext *component) override;
  bool componentCanUnload(RTT::TaskContext *component) override;
  void componentUnloaded(RTT::TaskContext *component) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  bool publishComponentImpl(
      const std::string &component_name,
      const std::vector<std::string> *selectors);
  bool fail(const char *operation, std::string error) const;
};

} // namespace OCL

#endif // OCL_OPCUADEPLOYMENTCOMPONENT_HPP
