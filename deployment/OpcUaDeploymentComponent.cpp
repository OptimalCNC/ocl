#include "OpcUaDeploymentComponent.hpp"

#include <rtt/Logger.hpp>
#include <rtt/Service.hpp>
#include <rtt/opcua/datatype_registry.hpp>
#include <rtt/opcua/type_protocol.hpp>

#include <algorithm>
#include <exception>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace OCL {

namespace {

std::vector<std::string> diagnosticMessages(
    const std::vector<RTT::opcua::UnsupportedResource> &diagnostics) {
  std::vector<std::string> messages;
  messages.reserve(diagnostics.size());
  std::transform(diagnostics.begin(), diagnostics.end(),
                 std::back_inserter(messages),
                 [](const RTT::opcua::UnsupportedResource &resource) {
                   return resource.message();
                 });
  return messages;
}

} // namespace

class OpcUaDeploymentComponent::Impl {
public:
  enum class State {
    created,
    starting,
    running,
    start_failed,
    stopping,
    destroyed,
  };

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
  std::map<std::string, RTT::TaskContext *, std::less<>> published;
  std::map<std::string, std::vector<std::string>, std::less<>> diagnostics;
  std::map<std::string, RemotePeer> remote_peers;
  mutable std::mutex mutex;
  State state{State::created};
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
      ->addOperation("isRunning", &OpcUaDeploymentComponent::opcUaIsRunning,
                     this, RTT::ClientThread)
      .doc("Reports whether the OPC UA deployment server is running.");
  opcua
      ->addOperation("endpointUrl", &OpcUaDeploymentComponent::opcUaEndpointUrl,
                     this, RTT::ClientThread)
      .doc("Returns the OPC UA endpoint URL.");
  opcua
      ->addOperation("lastError", &OpcUaDeploymentComponent::opcUaLastError,
                     this, RTT::ClientThread)
      .doc("Returns the most recent OPC UA deployment error.");
  opcua
      ->addOperation("unsupportedResources",
                     &OpcUaDeploymentComponent::unsupportedResources, this,
                     RTT::ClientThread)
      .doc("Returns diagnostics from a rejected component publication.")
      .arg("component", "RTT component name.");
  opcua
      ->addOperation("publishComponent",
                     &OpcUaDeploymentComponent::publishComponent, this,
                     RTT::ClientThread)
      .doc("Publishes one local RTT component on the running endpoint.")
      .arg("component", "Local RTT component name.");
}

OpcUaDeploymentComponent::~OpcUaDeploymentComponent() {
  if (!impl_) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->state = Impl::State::stopping;
  }

  impl_->server.stop();

  std::unique_ptr<RTT::opcua::ObjectModel> model;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    model = std::move(impl_->model);
  }
  model.reset();

  std::map<std::string, Impl::RemotePeer> remote_peers;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    remote_peers.swap(impl_->remote_peers);
    impl_->published.clear();
  }
  for (auto &[peer_name, remote] : remote_peers) {
    RTT::TaskContext::removePeer(peer_name);
    remote.proxy->disconnect();
  }
  remote_peers.clear();
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->state = Impl::State::destroyed;
  }
}

bool OpcUaDeploymentComponent::opcUaIsRunning() const {
  if (!impl_) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->state == Impl::State::running && impl_->server.isRunning();
}

std::string OpcUaDeploymentComponent::opcUaEndpointUrl() const {
  if (!impl_) {
    return {};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->server.endpointUrl();
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
  const auto found = impl_->diagnostics.find(component_name);
  return found == impl_->diagnostics.end() ? std::vector<std::string>{}
                                           : found->second;
}

bool OpcUaDeploymentComponent::startOpcUa() {
  if (!impl_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state == Impl::State::running) {
    if (!impl_->server.isRunning()) {
      impl_->last_error = impl_->server.lastError();
      if (impl_->last_error.empty()) {
        impl_->last_error = "OPC UA server is not running";
      }
      return false;
    }
    impl_->last_error.clear();
    return true;
  }
  if (impl_->state == Impl::State::stopping ||
      impl_->state == Impl::State::destroyed) {
    impl_->last_error = "OPC UA deployment service is stopping";
    return false;
  }

  impl_->state = Impl::State::starting;
  auto fail_start = [this](std::string error) {
    impl_->state = Impl::State::start_failed;
    impl_->last_error = std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::startOpcUa", "%s",
                            impl_->last_error.c_str());
    return false;
  };

  std::string error;
  if (!RTT::opcua::registerCanonicalTypeProtocols(&error)) {
    return fail_start(error.empty()
                          ? "failed to register canonical OPC UA types"
                          : std::move(error));
  }
  if (!RTT::opcua::freezeDataTypeRegistry(&error)) {
    return fail_start(error.empty()
                          ? "failed to freeze OPC UA datatype registry"
                          : std::move(error));
  }
  if (!impl_->server.start(&error)) {
    impl_->server.stop();
    return fail_start(error.empty() ? "failed to start OPC UA server"
                                    : std::move(error));
  }

  std::unique_ptr<RTT::opcua::ObjectModel> candidate;
  try {
    candidate = std::make_unique<RTT::opcua::ObjectModel>(
        impl_->server, impl_->options.object_model);
    std::vector<RTT::opcua::UnsupportedResource> diagnostics;
    if (!candidate->publishComponent(*this, &error, &diagnostics)) {
      impl_->diagnostics.insert_or_assign(getName(),
                                          diagnosticMessages(diagnostics));
      impl_->server.stop();
      candidate.reset();
      return fail_start(error.empty() ? "failed to publish OPC UA deployer"
                                      : std::move(error));
    }

    impl_->published.insert_or_assign(getName(), this);
    impl_->diagnostics.erase(getName());
    impl_->model = std::move(candidate);
    impl_->state = Impl::State::running;
    impl_->last_error.clear();
    return true;
  } catch (const std::exception &exception) {
    impl_->server.stop();
    candidate.reset();
    return fail_start(exception.what());
  } catch (...) {
    impl_->server.stop();
    candidate.reset();
    return fail_start("failed to construct OPC UA object model");
  }
}

bool OpcUaDeploymentComponent::publishComponent(
    const std::string &component_name) {
  if (!impl_) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->state != Impl::State::running || !impl_->model) {
    impl_->last_error = "OPC UA server is not running";
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::publishComponent", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  RTT::TaskContext *component =
      component_name == getName() ? this : getPeer(component_name);
  if (component == nullptr) {
    impl_->last_error = "no such local RTT component: " + component_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::publishComponent", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  if (dynamic_cast<RTT::opcua::TaskContextProxy *>(component) != nullptr) {
    impl_->last_error =
        "refusing to publish remote OPC UA proxy: " + component_name;
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::publishComponent", "%s",
                            impl_->last_error.c_str());
    return false;
  }
  const auto found = impl_->published.find(component_name);
  if (found != impl_->published.end()) {
    if (found->second != component) {
      impl_->last_error =
          "another RTT component instance is already published as: " +
          component_name;
      RTT::Logger::log().logf(RTT::Logger::Error,
                              "OpcUaDeploymentComponent::publishComponent",
                              "%s", impl_->last_error.c_str());
      return false;
    }
    impl_->last_error.clear();
    return true;
  }

  std::vector<RTT::opcua::UnsupportedResource> diagnostics;
  std::string error;
  if (!impl_->model->publishComponent(*component, &error, &diagnostics)) {
    impl_->diagnostics.insert_or_assign(component_name,
                                        diagnosticMessages(diagnostics));
    impl_->last_error =
        error.empty() ? "failed to publish RTT component" : std::move(error);
    RTT::Logger::log().logf(RTT::Logger::Error,
                            "OpcUaDeploymentComponent::publishComponent", "%s",
                            impl_->last_error.c_str());
    return false;
  }

  impl_->published.emplace(component_name, component);
  impl_->diagnostics.erase(component_name);
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
  return true;
}

bool OpcUaDeploymentComponent::componentCanUnload(RTT::TaskContext *component) {
  if (!impl_ || component == nullptr) {
    return true;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto published = impl_->published.find(component->getName());
  if (published == impl_->published.end() || published->second != component) {
    return true;
  }

  impl_->last_error = "Cannot unload component '" + component->getName() +
                      "': it is published through OPC UA";
  RTT::Logger::log().logf(RTT::Logger::Error,
                          "OpcUaDeploymentComponent::componentCanUnload", "%s",
                          impl_->last_error.c_str());
  return false;
}

void OpcUaDeploymentComponent::componentUnloaded(RTT::TaskContext *) {}

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
