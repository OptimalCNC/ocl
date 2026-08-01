#include "OpcUaDeploymentComponent.hpp"

#include <rtt/Logger.hpp>
#include <rtt/Service.hpp>

#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace OCL {

class OpcUaDeploymentComponent::Impl {
public:
  struct RemotePeer {
    std::string endpoint_url;
    std::string component_name;
    std::unique_ptr<RTT::opcua::TaskContextProxy> proxy;
  };

  explicit Impl(OpcUaDeploymentOptions configured_options)
      : options(std::move(configured_options)), server(options.server) {
    std::string error;
    if (!server.start(&error)) {
      throw std::runtime_error("failed to start OPC UA deployment server: " +
                               error);
    }
    model =
        std::make_unique<RTT::opcua::ObjectModel>(server, options.object_model);
  }

  OpcUaDeploymentOptions options;
  RTT::opcua::Server server;
  std::unique_ptr<RTT::opcua::ObjectModel> model;
  std::map<RTT::TaskContext *, RTT::opcua::ComponentRegistration> published;
  std::map<std::string, RemotePeer> remote_peers;
  mutable std::mutex mutex;
  std::string last_error;
};

OpcUaDeploymentComponent::OpcUaDeploymentComponent(
    const std::string &name, const std::string &site_file,
    OpcUaDeploymentOptions options)
    : DeploymentComponent(name, site_file),
      impl_(std::make_unique<Impl>(std::move(options))) {
  RTT::Service::shared_ptr opcua = RTT::Service::Create("opcua", this);
  opcua->doc("Publishes RTT components and manages remote OPC UA peers.");
  opcua
      ->addOperation("ready", &OpcUaDeploymentComponent::opcUaReady, this,
                     RTT::ClientThread)
      .doc("Reports whether the OPC UA deployment server is running.");
  opcua
      ->addOperation("endpoint", &OpcUaDeploymentComponent::opcUaEndpoint, this,
                     RTT::ClientThread)
      .doc("Returns the OPC UA endpoint URL.");
  opcua
      ->addOperation("lastError", &OpcUaDeploymentComponent::opcUaLastError,
                     this, RTT::ClientThread)
      .doc("Returns the most recent OPC UA deployment error.");
  opcua
      ->addOperation("publishPeer", &OpcUaDeploymentComponent::publishPeer,
                     this, RTT::ClientThread)
      .doc("Publishes a deployer peer on this OPC UA endpoint.")
      .arg("peer", "Peer name known to this deployer.");
  opcua
      ->addOperation("unpublishPeer", &OpcUaDeploymentComponent::unpublishPeer,
                     this, RTT::ClientThread)
      .doc("Removes a deployer peer from this OPC UA endpoint.")
      .arg("peer", "Peer name known to this deployer.");
  opcua
      ->addOperation("connectRemote", &OpcUaDeploymentComponent::connectRemote,
                     this, RTT::ClientThread)
      .doc("Creates an owned proxy for a remote OPC UA component.")
      .arg("endpoint", "Remote OPC UA endpoint URL.")
      .arg("component", "Remote RTT component name.")
      .arg("peer", "Local peer name, or empty to use the component name.");
  opcua
      ->addOperation("disconnectRemote",
                     &OpcUaDeploymentComponent::disconnectRemote, this,
                     RTT::ClientThread)
      .doc("Disconnects and destroys an OPC UA remote peer.")
      .arg("peer", "Local peer name.");
  opcua
      ->addOperation("synchronizeRemote",
                     &OpcUaDeploymentComponent::synchronizeRemote, this,
                     RTT::ClientThread)
      .doc("Refreshes an OPC UA remote peer interface atomically.")
      .arg("peer", "Local peer name.");

  if (!publishComponent(*this)) {
    throw std::runtime_error("failed to publish OPC UA deployer: " +
                             opcUaLastError());
  }

  // DeploymentComponent loads the site file in its base constructor, before
  // this subclass can receive componentLoaded() callbacks.
  for (const auto &[component_name, component] : compmap) {
    if (component.instance == nullptr || !component.server || component.proxy) {
      continue;
    }
    if (!publishComponent(*component.instance)) {
      throw std::runtime_error("failed to publish site component '" +
                               component_name + "': " + opcUaLastError());
    }
  }
}

OpcUaDeploymentComponent::~OpcUaDeploymentComponent() {
  if (!impl_) {
    return;
  }

  std::map<std::string, Impl::RemotePeer> remote_peers;
  std::map<RTT::TaskContext *, RTT::opcua::ComponentRegistration> published;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    remote_peers.swap(impl_->remote_peers);
    published.swap(impl_->published);
  }
  for (auto &[peer_name, remote] : remote_peers) {
    RTT::TaskContext::removePeer(peer_name);
    remote.proxy->disconnect();
  }
  remote_peers.clear();
  published.clear();
}

bool OpcUaDeploymentComponent::opcUaReady() const {
  return impl_ && impl_->server.isRunning();
}

std::string OpcUaDeploymentComponent::opcUaEndpoint() const {
  return impl_ ? impl_->server.endpointUrl() : std::string{};
}

std::string OpcUaDeploymentComponent::opcUaLastError() const {
  if (!impl_) {
    return "OPC UA deployment service is unavailable";
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_error;
}

bool OpcUaDeploymentComponent::publishPeer(const std::string &peer_name) {
  RTT::TaskContext *component =
      peer_name == "this" || peer_name == getName() ? this : getPeer(peer_name);
  if (component == nullptr) {
    return fail("publishPeer", "no such deployer peer: " + peer_name);
  }
  if (dynamic_cast<RTT::opcua::TaskContextProxy *>(component) != nullptr) {
    return fail("publishPeer",
                "refusing to republish remote proxy: " + peer_name);
  }
  return publishComponent(*component);
}

bool OpcUaDeploymentComponent::unpublishPeer(const std::string &peer_name) {
  RTT::TaskContext *component =
      peer_name == "this" || peer_name == getName() ? this : getPeer(peer_name);
  if (component == nullptr) {
    return fail("unpublishPeer", "no such deployer peer: " + peer_name);
  }
  if (component == this) {
    return fail("unpublishPeer",
                "the deployer endpoint cannot unpublish itself");
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto found = impl_->published.find(component);
  if (found == impl_->published.end()) {
    impl_->last_error = "peer is not published: " + peer_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::unpublishPeer", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  impl_->published.erase(found);
  impl_->last_error.clear();
  return true;
}

bool OpcUaDeploymentComponent::connectRemote(
    const std::string &endpoint_url, const std::string &component_name,
    const std::string &requested_peer_name) {
  if (endpoint_url.empty()) {
    return fail("connectRemote", "endpoint URL must not be empty");
  }
  if (component_name.empty()) {
    return fail("connectRemote", "remote component name must not be empty");
  }
  const std::string peer_name =
      requested_peer_name.empty() ? component_name : requested_peer_name;

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto existing = impl_->remote_peers.find(peer_name);
  if (existing != impl_->remote_peers.end()) {
    if (existing->second.endpoint_url != endpoint_url ||
        existing->second.component_name != component_name) {
      impl_->last_error =
          "peer name is already used by another remote: " + peer_name;
      RTT::Logger::log().logf(RTT::Logger::Error,
                              "OpcUaDeploymentComponent::connectRemote", "%s",
                              impl_->last_error.c_str());
      return false;
    }
    std::string error;
    if (!existing->second.proxy->synchronize(&error)) {
      impl_->last_error = std::move(error);
      return false;
    }
    impl_->last_error.clear();
    return true;
  }
  if (getPeer(peer_name) != nullptr) {
    impl_->last_error = "peer name is already in use: " + peer_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::connectRemote", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  std::string error;
  auto proxy = RTT::opcua::TaskContextProxy::create(
      endpoint_url, component_name, impl_->options.proxy, &error);
  if (!proxy) {
    impl_->last_error =
        error.empty() ? "failed to create remote proxy" : std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::connectRemote", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  auto [inserted, was_inserted] = impl_->remote_peers.emplace(
      peer_name,
      Impl::RemotePeer{endpoint_url, component_name, std::move(proxy)});
  if (!was_inserted ||
      !RTT::TaskContext::addPeer(inserted->second.proxy.get(), peer_name)) {
    impl_->remote_peers.erase(peer_name);
    impl_->last_error = "failed to add remote deployer peer: " + peer_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::connectRemote", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  impl_->last_error.clear();
  return true;
}

bool OpcUaDeploymentComponent::disconnectRemote(const std::string &peer_name) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto found = impl_->remote_peers.find(peer_name);
  if (found == impl_->remote_peers.end()) {
    impl_->last_error = "no such OPC UA remote peer: " + peer_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::disconnectRemote", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  RTT::TaskContext::removePeer(peer_name);
  found->second.proxy->disconnect();
  impl_->remote_peers.erase(found);
  impl_->last_error.clear();
  return true;
}

bool OpcUaDeploymentComponent::synchronizeRemote(const std::string &peer_name) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto found = impl_->remote_peers.find(peer_name);
  if (found == impl_->remote_peers.end()) {
    impl_->last_error = "no such OPC UA remote peer: " + peer_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::synchronizeRemote", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  std::string error;
  if (!found->second.proxy->synchronize(&error)) {
    impl_->last_error =
        error.empty() ? "failed to synchronize remote peer" : std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::synchronizeRemote", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  impl_->last_error.clear();
  return true;
}

bool OpcUaDeploymentComponent::componentLoaded(RTT::TaskContext *component) {
  if (component == nullptr) {
    return false;
  }
  if (dynamic_cast<RTT::opcua::TaskContextProxy *>(component) != nullptr) {
    for (CompMap::iterator entry = compmap.begin(); entry != compmap.end();
         ++entry) {
      if (entry->second.instance == component) {
        entry->second.proxy = true;
        return true;
      }
    }
    return false;
  }

  const auto configured = compmap.find(component->getName());
  if (configured != compmap.end() && configured->second.server) {
    return publishComponent(*component);
  }
  return true;
}

void OpcUaDeploymentComponent::componentUnloaded(RTT::TaskContext *component) {
  unpublishComponent(component);
}

bool OpcUaDeploymentComponent::publishComponent(RTT::TaskContext &component) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->published.find(&component) != impl_->published.end()) {
    impl_->last_error.clear();
    return true;
  }

  std::string error;
  auto registration = impl_->model->registerComponent(component, &error);
  if (!registration) {
    impl_->last_error =
        error.empty() ? "failed to publish RTT component" : std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::publishComponent", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  impl_->published.emplace(&component, std::move(*registration));
  impl_->last_error.clear();
  return true;
}

void OpcUaDeploymentComponent::unpublishComponent(
    RTT::TaskContext *component) noexcept {
  if (!impl_ || component == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->published.erase(component);
}

bool OpcUaDeploymentComponent::fail(const char *operation,
                                    std::string error) const {
  if (!impl_) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->last_error = std::move(error);
  RTT::Logger::log().logf(RTT::Logger::Error, operation, "%s",
                          impl_->last_error.c_str());
  return false;
}

} // namespace OCL
