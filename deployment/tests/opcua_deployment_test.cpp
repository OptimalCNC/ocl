#define BOOST_TEST_MODULE ocl_opcua_deployment
#include <boost/test/included/unit_test.hpp>

#include "deployment/OpcUaDeploymentComponent.hpp"

#include <rtt/OperationCaller.hpp>
#include <rtt/Property.hpp>
#include <rtt/Service.hpp>
#include <rtt/TaskContext.hpp>
#include <rtt/deployment/ComponentLoader.hpp>
#include <rtt/opcua/datatype_registry.hpp>
#include <rtt/opcua/object_model.hpp>
#include <rtt/opcua/server.hpp>
#include <rtt/opcua/task_context_proxy.hpp>
#include <rtt/typekit/RealTimeTypekit.hpp>
#include <rtt/types/TemplateTypeInfo.hpp>
#include <rtt/types/Types.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct UnsupportedValue {
  std::int32_t value{0};
};

constexpr std::string_view kUnsupportedTypeName = "/test/OclUnsupportedValue";

std::uint16_t unusedLoopbackPort() {
  const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    throw std::runtime_error("failed to create test socket");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(socket_fd, reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) != 0) {
    ::close(socket_fd);
    throw std::runtime_error("failed to bind test socket");
  }

  socklen_t size = sizeof(address);
  if (::getsockname(socket_fd, reinterpret_cast<sockaddr *>(&address), &size) !=
      0) {
    ::close(socket_fd);
    throw std::runtime_error("failed to inspect test socket");
  }
  const std::uint16_t port = ntohs(address.sin_port);
  ::close(socket_fd);
  return port;
}

class OccupiedLoopbackPort final {
public:
  OccupiedLoopbackPort() : socket_fd_(::socket(AF_INET, SOCK_STREAM, 0)) {
    if (socket_fd_ < 0) {
      throw std::runtime_error("failed to create occupied-port socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket_fd_, reinterpret_cast<const sockaddr *>(&address),
               sizeof(address)) != 0 ||
        ::listen(socket_fd_, 1) != 0) {
      release();
      throw std::runtime_error("failed to occupy loopback port");
    }

    socklen_t size = sizeof(address);
    if (::getsockname(socket_fd_, reinterpret_cast<sockaddr *>(&address),
                      &size) != 0) {
      release();
      throw std::runtime_error("failed to inspect occupied loopback port");
    }
    port_ = ntohs(address.sin_port);
  }

  ~OccupiedLoopbackPort() { release(); }

  OccupiedLoopbackPort(const OccupiedLoopbackPort &) = delete;
  OccupiedLoopbackPort &operator=(const OccupiedLoopbackPort &) = delete;

  std::uint16_t port() const noexcept { return port_; }

  void release() noexcept {
    if (socket_fd_ >= 0) {
      ::close(socket_fd_);
      socket_fd_ = -1;
    }
  }

private:
  int socket_fd_{-1};
  std::uint16_t port_{0U};
};

void loadRttTypes(bool add_unsupported_type = false) {
  if (RTT::types::Types()->type("Int32") == nullptr) {
    RTT::types::RealTimeTypekitPlugin().loadTypes();
  }
  if (add_unsupported_type &&
      RTT::types::Types()->type(std::string(kUnsupportedTypeName)) == nullptr) {
    BOOST_REQUIRE(RTT::types::Types()->addType(
        new RTT::types::TemplateTypeInfo<UnsupportedValue, false>(
            std::string(kUnsupportedTypeName))));
  }
}

class EchoTask final : public RTT::TaskContext {
public:
  explicit EchoTask(const std::string &name) : RTT::TaskContext(name) {
    addOperation("echo", &EchoTask::echo, this, RTT::ClientThread);
    addProperty("Gain", gain);
  }

  void addLateResource() {
    addOperation("lateEcho", &EchoTask::lateEcho, this, RTT::ClientThread);
  }

  std::int32_t gain{7};

private:
  std::int32_t echo(std::int32_t value) { return value; }
  std::int32_t lateEcho(std::int32_t value) { return value + 1; }
};

class UnsupportedTask final : public RTT::TaskContext {
public:
  UnsupportedTask() : RTT::TaskContext("UnsupportedPeer") {
    addProperty("Value", value);
  }

private:
  UnsupportedValue value{7};
};

RTT::TaskContext *createEchoTask(std::string name) {
  return new EchoTask(std::move(name));
}

class FactoryRegistration final {
public:
  FactoryRegistration(std::string name, RTT::ComponentLoaderSignature factory)
      : name_(std::move(name)) {
    RTT::ComponentLoader::Instance()->addFactory(name_, factory);
  }

  ~FactoryRegistration() {
    auto &factories = const_cast<RTT::FactoryMap &>(
        RTT::ComponentLoader::Instance()->getFactories());
    factories.erase(name_);
  }

  FactoryRegistration(const FactoryRegistration &) = delete;
  FactoryRegistration &operator=(const FactoryRegistration &) = delete;

private:
  std::string name_;
};

class TemporarySiteFile final {
public:
  explicit TemporarySiteFile(std::uint16_t port)
      : path_(std::filesystem::temp_directory_path() /
              ("ocl-opcua-site-" + std::to_string(::getpid()) + "-" +
               std::to_string(port) + ".cpf")) {
    std::ofstream output(path_);
    output << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE properties SYSTEM "cpf.dtd">
<properties>
  <struct name="SiteEcho" type="TestEchoTask">
    <simple name="Server" type="boolean"><value>1</value></simple>
  </struct>
</properties>
)";
    if (!output) {
      throw std::runtime_error("failed to write temporary site file");
    }
  }

  ~TemporarySiteFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

OCL::OpcUaDeploymentOptions
deploymentOptions(std::uint16_t port = unusedLoopbackPort()) {
  OCL::OpcUaDeploymentOptions options;
  options.server.port = port;
  options.proxy.request_timeout = std::chrono::milliseconds(500);
  return options;
}

std::vector<std::string> operationNames(RTT::Service::shared_ptr service) {
  std::vector<std::string> names = service->getNames();
  std::sort(names.begin(), names.end());
  return names;
}

const std::vector<std::string> kExpectedOpcUaOperations{
    "endpointUrl",      "isRunning", "lastError",
    "publishComponent", "start",     "unsupportedResources"};

} // namespace

BOOST_AUTO_TEST_CASE(explicit_start_publishes_only_deployer) {
  loadRttTypes();
  EchoTask local("LocalEcho");
  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());

  RTT::Service::shared_ptr local_opcua = deployer.provides("opcua");
  BOOST_REQUIRE(local_opcua != nullptr);
  BOOST_TEST(operationNames(local_opcua) == kExpectedOpcUaOperations);
  BOOST_TEST(!deployer.opcUaIsRunning());
  BOOST_TEST(deployer.opcUaEndpointUrl().find("opc.tcp://127.0.0.1:") == 0U);
  BOOST_REQUIRE(deployer.addPeer(&local));
  BOOST_TEST(!deployer.publishComponent(local.getName()));
  BOOST_TEST(deployer.opcUaLastError() == "OPC UA server is not running");

  BOOST_REQUIRE(deployer.startOpcUa());
  BOOST_TEST(deployer.opcUaIsRunning());
  BOOST_TEST(deployer.opcUaLastError().empty());

  std::string error;
  auto deployer_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpointUrl(), deployer.getName(), {}, &error);
  BOOST_REQUIRE_MESSAGE(deployer_proxy != nullptr, error);
  RTT::Service::shared_ptr remote_opcua = deployer_proxy->provides("opcua");
  BOOST_REQUIRE(remote_opcua != nullptr);
  BOOST_TEST(operationNames(remote_opcua) == kExpectedOpcUaOperations);

  auto local_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpointUrl(), local.getName(), {}, &error);
  BOOST_TEST(local_proxy == nullptr);
  BOOST_TEST(!error.empty());

  const std::string endpoint = deployer.opcUaEndpointUrl();
  BOOST_TEST(!deployer.publishComponent("MissingPeer"));
  BOOST_TEST(!deployer.opcUaLastError().empty());
  BOOST_TEST(deployer.startOpcUa());
  BOOST_TEST(deployer.opcUaLastError().empty());
  BOOST_TEST(deployer.opcUaEndpointUrl() == endpoint);
}

BOOST_AUTO_TEST_CASE(strict_publication_is_static_and_idempotent) {
  loadRttTypes(true);
  EchoTask local("LocalEcho");
  UnsupportedTask unsupported;
  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());
  BOOST_REQUIRE(deployer.addPeer(&local));
  BOOST_REQUIRE(deployer.addPeer(&unsupported));
  BOOST_REQUIRE(deployer.startOpcUa());

  BOOST_TEST(!deployer.publishComponent("MissingPeer"));
  BOOST_TEST(deployer.opcUaLastError() ==
             "no such local RTT component: MissingPeer");
  BOOST_REQUIRE(deployer.publishComponent(local.getName()));

  std::string error;
  auto local_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpointUrl(), local.getName(), {}, &error);
  BOOST_REQUIRE_MESSAGE(local_proxy != nullptr, error);
  RTT::OperationCaller<std::int32_t(std::int32_t)> echo =
      local_proxy->getOperation("echo");
  BOOST_REQUIRE(echo.ready());
  BOOST_TEST(echo(42) == 42);
  auto *gain = dynamic_cast<RTT::Property<std::int32_t> *>(
      local_proxy->provides()->getProperty("Gain"));
  BOOST_REQUIRE(gain != nullptr);
  gain->set(9);
  BOOST_TEST(local.gain == 9);

  local.addLateResource();
  BOOST_TEST(deployer.publishComponent(local.getName()));
  BOOST_REQUIRE_MESSAGE(local_proxy->synchronize(&error), error);
  BOOST_TEST(!local_proxy->provides()->hasMember("lateEcho"));

  BOOST_TEST(!deployer.publishComponent(unsupported.getName()));
  BOOST_TEST(deployer.opcUaLastError() ==
             "strict OPC UA publication rejected component 'UnsupportedPeer'");
  const std::vector<std::string> expected_diagnostics{
      "OPC UA: component 'UnsupportedPeer' rejected property 'Value' because "
      "RTT type '/test/OclUnsupportedValue' has no registered OPC UA "
      "protocol."};
  BOOST_TEST(deployer.unsupportedResources(unsupported.getName()) ==
             expected_diagnostics);
  auto unsupported_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpointUrl(), unsupported.getName(), {}, &error);
  BOOST_TEST(unsupported_proxy == nullptr);
  BOOST_TEST(!error.empty());

  BOOST_REQUIRE(deployer.addPeer(local_proxy.get(), "RemoteAlias"));
  BOOST_TEST(!deployer.publishComponent("RemoteAlias"));
  BOOST_TEST(deployer.opcUaLastError() ==
             "refusing to publish remote OPC UA proxy: RemoteAlias");
}

BOOST_AUTO_TEST_CASE(server_metadata_does_not_auto_publish) {
  loadRttTypes();
  RTT::ComponentLoader::Instance()->addFactory("TestEchoTask", &createEchoTask);
  OCL::OpcUaDeploymentOptions options = deploymentOptions();
  TemporarySiteFile site_file(options.server.port);
  OCL::OpcUaDeploymentComponent deployer("Deployer", site_file.path().string(),
                                         options);

  BOOST_TEST(!deployer.opcUaIsRunning());
  BOOST_REQUIRE(deployer.getPeer("SiteEcho") != nullptr);
  BOOST_REQUIRE(deployer.startOpcUa());

  std::string error;
  auto site_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpointUrl(), "SiteEcho", {}, &error);
  BOOST_TEST(site_proxy == nullptr);
  BOOST_TEST(!error.empty());

  BOOST_REQUIRE(deployer.publishComponent("SiteEcho"));
  site_proxy = RTT::opcua::TaskContextProxy::create(deployer.opcUaEndpointUrl(),
                                                    "SiteEcho", {}, &error);
  BOOST_REQUIRE_MESSAGE(site_proxy != nullptr, error);
  RTT::OperationCaller<std::int32_t(std::int32_t)> echo =
      site_proxy->getOperation("echo");
  BOOST_REQUIRE(echo.ready());
  BOOST_TEST(echo(84) == 84);
}

BOOST_AUTO_TEST_CASE(failed_start_freezes_registry_and_can_retry) {
  loadRttTypes();
  OccupiedLoopbackPort occupied;
  OCL::OpcUaDeploymentComponent deployer("Deployer", "",
                                         deploymentOptions(occupied.port()));

  BOOST_TEST(!RTT::opcua::dataTypeRegistryFrozen());
  BOOST_TEST(!deployer.startOpcUa());
  BOOST_TEST(!deployer.opcUaIsRunning());
  BOOST_TEST(RTT::opcua::dataTypeRegistryFrozen());
  const std::string startup_error = deployer.opcUaLastError();
  BOOST_TEST(!startup_error.empty());
  BOOST_TEST(deployer.opcUaLastError() == startup_error);

  occupied.release();
  BOOST_REQUIRE(deployer.startOpcUa());
  BOOST_TEST(deployer.opcUaIsRunning());
  BOOST_TEST(RTT::opcua::dataTypeRegistryFrozen());
  BOOST_TEST(deployer.opcUaLastError().empty());
}

BOOST_AUTO_TEST_CASE(remote_components_remain_aliased_client_peers) {
  loadRttTypes();
  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());
  BOOST_REQUIRE(deployer.startOpcUa());

  RTT::opcua::ServerOptions remote_options;
  remote_options.port = unusedLoopbackPort();
  RTT::opcua::Server remote_server(remote_options);
  std::string error;
  BOOST_REQUIRE_MESSAGE(remote_server.start(&error), error);
  EchoTask remote("RemoteEcho");
  RTT::opcua::ObjectModel remote_model(remote_server);
  BOOST_REQUIRE_MESSAGE(remote_model.publishComponent(remote, &error), error);

  BOOST_REQUIRE(deployer.connectRemote(remote_server.endpointUrl(),
                                       remote.getName(), "RemoteAlias"));
  RTT::TaskContext *peer = deployer.getPeer("RemoteAlias");
  BOOST_REQUIRE(peer != nullptr);
  BOOST_TEST(peer->getName() == remote.getName());
  BOOST_REQUIRE(dynamic_cast<RTT::opcua::TaskContextProxy *>(peer) != nullptr);
  RTT::OperationCaller<std::int32_t(std::int32_t)> echo =
      peer->getOperation("echo");
  BOOST_REQUIRE(echo.ready());
  BOOST_TEST(echo(7) == 7);

  BOOST_TEST(deployer.connectRemote(remote_server.endpointUrl(),
                                    remote.getName(), "RemoteAlias"));
  BOOST_TEST(deployer.synchronizeRemote("RemoteAlias"));
  BOOST_TEST(!deployer.publishComponent("RemoteAlias"));
  BOOST_REQUIRE(deployer.disconnectRemote("RemoteAlias"));
  BOOST_TEST(deployer.getPeer("RemoteAlias") == nullptr);
  BOOST_TEST(!deployer.disconnectRemote("RemoteAlias"));
  BOOST_TEST(!deployer.opcUaLastError().empty());
  remote_server.stop();
}

BOOST_AUTO_TEST_CASE(published_component_unload_is_rejected) {
  loadRttTypes();
  FactoryRegistration factory("TestEchoTask", &createEchoTask);

  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());
  BOOST_REQUIRE(deployer.loadComponent("DisposableEcho", "TestEchoTask"));
  BOOST_REQUIRE(deployer.unloadComponent("DisposableEcho"));
  BOOST_TEST(deployer.getPeer("DisposableEcho") == nullptr);

  BOOST_REQUIRE(deployer.loadComponent("ManagedEcho", "TestEchoTask"));
  RTT::TaskContext *const managed = deployer.getPeer("ManagedEcho");
  BOOST_REQUIRE(managed != nullptr);
  BOOST_REQUIRE(deployer.startOpcUa());
  BOOST_REQUIRE(deployer.publishComponent("ManagedEcho"));

  std::string error;
  auto proxy = RTT::opcua::TaskContextProxy::create(deployer.opcUaEndpointUrl(),
                                                    "ManagedEcho", {}, &error);
  BOOST_REQUIRE_MESSAGE(proxy != nullptr, error);

  BOOST_REQUIRE(!deployer.unloadComponent("ManagedEcho"));
  BOOST_TEST(deployer.opcUaLastError() ==
             "Cannot unload component 'ManagedEcho': it is published through "
             "OPC UA");
  BOOST_TEST(deployer.getPeer("ManagedEcho") == managed);

  RTT::OperationCaller<std::int32_t(std::int32_t)> echo =
      proxy->getOperation("echo");
  BOOST_REQUIRE(echo.ready());
  BOOST_TEST(echo(21) == 21);
}
