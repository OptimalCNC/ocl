#ifndef OCL_OPCUADEPLOYMENTCOMPONENT_HPP
#define OCL_OPCUADEPLOYMENTCOMPONENT_HPP

#include "DeploymentComponent.hpp"

#include <rtt/opcua/object_model.hpp>
#include <rtt/opcua/server_options.hpp>
#include <rtt/opcua/task_context_proxy.hpp>

#include <memory>
#include <string>

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

  bool opcUaReady() const;
  std::string opcUaEndpoint() const;
  std::string opcUaLastError() const;

  bool publishPeer(const std::string &peer_name);
  bool unpublishPeer(const std::string &peer_name);

  bool connectRemote(const std::string &endpoint_url,
                     const std::string &component_name,
                     const std::string &peer_name);
  bool disconnectRemote(const std::string &peer_name);
  bool synchronizeRemote(const std::string &peer_name);

protected:
  bool componentLoaded(RTT::TaskContext *component) override;
  void componentUnloaded(RTT::TaskContext *component) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  bool publishComponent(RTT::TaskContext &component);
  void unpublishComponent(RTT::TaskContext *component) noexcept;
  bool fail(const char *operation, std::string error) const;
};

} // namespace OCL

#endif // OCL_OPCUADEPLOYMENTCOMPONENT_HPP
