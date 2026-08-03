#include "OpcUaDeploymentComponent.hpp"

#include <rtt/Logger.hpp>
#include <rtt/Service.hpp>
#include <rtt/opcua/type_protocol.hpp>

#include <algorithm>
#include <exception>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace OCL {

class OpcUaDeploymentComponent::Impl {
public:
  struct RemotePeer {
    std::string endpoint_url;
    std::string component_name;
    std::unique_ptr<RTT::opcua::TaskContextProxy> proxy;
  };

  explicit Impl(OpcUaDeploymentOptions configured_options)
      : options(std::move(configured_options)), server(options.server) {}

  OpcUaDeploymentOptions options;
  RTT::opcua::Server server;
  std::unique_ptr<RTT::opcua::ObjectModel> model;
  std::map<std::string, RTT::TaskContext *, std::less<>> pending;
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
      ->addOperation("start", &OpcUaDeploymentComponent::startOpcUa, this,
                     RTT::ClientThread)
      .doc("Starts the OPC UA endpoint after local package imports complete.");
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
      ->addOperation("unsupportedResources",
                     &OpcUaDeploymentComponent::unsupportedResources, this,
                     RTT::ClientThread)
      .doc("Returns resources omitted from an OPC UA component publication.")
      .arg("component", "Published RTT component name.");
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

std::vector<std::string> OpcUaDeploymentComponent::unsupportedResources(
    const std::string &component_name) const {
  if (!impl_) {
    return {};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto published =
      std::find_if(impl_->published.begin(), impl_->published.end(),
                   [&component_name](const auto &entry) {
                     return entry.second.name() == component_name;
                   });
  if (!impl_->model || published == impl_->published.end()) {
    impl_->last_error = "no such published OPC UA component: " + component_name;
    return {};
  }

  const std::vector<RTT::opcua::UnsupportedResource> diagnostics =
      impl_->model->unsupportedResources(component_name);
  std::vector<std::string> messages;
  messages.reserve(diagnostics.size());
  std::transform(diagnostics.begin(), diagnostics.end(),
                 std::back_inserter(messages),
                 [](const RTT::opcua::UnsupportedResource &resource) {
                   return resource.message();
                 });
  impl_->last_error.clear();
  return messages;
}

bool OpcUaDeploymentComponent::startOpcUa() {
  if (!impl_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->server.isRunning() && impl_->model) {
    impl_->last_error.clear();
    return true;
  }

  std::string error;
  if (!RTT::opcua::registerCanonicalTypeProtocols(&error)) {
    impl_->last_error = error.empty()
                            ? "failed to register canonical OPC UA types"
                            : std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::startOpcUa", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  if (!impl_->server.start(&error)) {
    impl_->last_error =
        error.empty() ? "failed to start OPC UA server" : std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::startOpcUa", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  try {
    auto model = std::make_unique<RTT::opcua::ObjectModel>(
        impl_->server, impl_->options.object_model);
    std::map<RTT::TaskContext *, RTT::opcua::ComponentRegistration> published;
    for (const auto &[name, component] : impl_->pending) {
      auto registration = model->registerComponent(*component, &error);
      if (!registration) {
        published.clear();
        model.reset();
        impl_->server.stop();
        impl_->last_error =
            "failed to publish OPC UA component '" + name +
            "': " + (error.empty() ? "unknown error" : std::move(error));
        RTT::Logger::log().logf(RTT::Logger::Error,
                                "OpcUaDeploymentComponent::startOpcUa", "%s",
                                impl_->last_error.c_str());
        return false;
      }
      published.emplace(component, std::move(*registration));
    }

    impl_->model = std::move(model);
    impl_->published = std::move(published);
    impl_->pending.clear();
    impl_->last_error.clear();
    return true;
  } catch (const std::exception &exception) {
    impl_->server.stop();
    impl_->last_error = "failed to construct OPC UA object model: " +
                        std::string(exception.what());
  } catch (...) {
    impl_->server.stop();
    impl_->last_error =
        "failed to construct OPC UA object model: unknown exception";
  }
  RTT::Logger::log().logf(RTT::Logger::Error,
                          "OpcUaDeploymentComponent::startOpcUa", "%s",
                          impl_->last_error.c_str());
  return false;
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
  if (found != impl_->published.end()) {
    impl_->published.erase(found);
    impl_->last_error.clear();
    return true;
  }
  const auto pending = impl_->pending.find(component->getName());
  if (pending == impl_->pending.end() || pending->second != component) {
    impl_->last_error = "peer is not published: " + peer_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::unpublishPeer", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  impl_->pending.erase(pending);
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
  const auto pending = impl_->pending.find(component.getName());
  if (pending != impl_->pending.end()) {
    if (pending->second != &component) {
      impl_->last_error =
          "another component is queued with name: " + component.getName();
      RTT::Logger::log().logf(RTT::Logger::Error,
                              "OpcUaDeploymentComponent::publishComponent",
                              "%s", impl_->last_error.c_str());
      return false;
    }
    impl_->last_error.clear();
    return true;
  }
  if (!impl_->model) {
    impl_->pending.emplace(component.getName(), &component);
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
  const auto pending = impl_->pending.find(component->getName());
  if (pending != impl_->pending.end() && pending->second == component) {
    impl_->pending.erase(pending);
  }
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
