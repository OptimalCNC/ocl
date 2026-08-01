#define BOOST_TEST_MODULE ocl_opcua_deployment
#include <boost/test/included/unit_test.hpp>

#include "deployment/OpcUaDeploymentComponent.hpp"

#include <rtt/OperationCaller.hpp>
#include <rtt/TaskContext.hpp>
#include <rtt/deployment/ComponentLoader.hpp>
#include <rtt/opcua/object_model.hpp>
#include <rtt/opcua/server.hpp>
#include <rtt/opcua/task_context_proxy.hpp>
#include <rtt/typekit/RealTimeTypekit.hpp>
#include <rtt/types/Types.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

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

void loadCanonicalTypes() {
  if (RTT::types::Types()->type("Int32") == nullptr) {
    RTT::types::RealTimeTypekitPlugin().loadTypes();
  }
}

class EchoTask final : public RTT::TaskContext {
public:
  explicit EchoTask(const std::string &name) : RTT::TaskContext(name) {
    addOperation("echo", &EchoTask::echo, this, RTT::ClientThread);
  }

private:
  std::int32_t echo(std::int32_t value) { return value; }
};

RTT::TaskContext *createEchoTask(std::string name) {
  return new EchoTask(std::move(name));
}

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

OCL::OpcUaDeploymentOptions deploymentOptions() {
  OCL::OpcUaDeploymentOptions options;
  options.server.port = unusedLoopbackPort();
  options.object_model.reconcile_interval = std::chrono::milliseconds(10);
  options.proxy.request_timeout = std::chrono::milliseconds(500);
  return options;
}

} // namespace

BOOST_AUTO_TEST_CASE(deployer_and_selected_local_peers_are_published) {
  loadCanonicalTypes();
  EchoTask local("LocalEcho");
  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());

  BOOST_TEST(deployer.opcUaReady());
  BOOST_TEST(deployer.opcUaEndpoint().find("opc.tcp://127.0.0.1:") == 0U);

  std::string error;
  auto deployer_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpoint(), deployer.getName(), {}, &error);
  BOOST_REQUIRE_MESSAGE(deployer_proxy != nullptr, error);
  RTT::Service::shared_ptr opcua = deployer_proxy->provides("opcua");
  BOOST_REQUIRE(opcua != nullptr);
  RTT::OperationCaller<bool()> ready = opcua->getOperation("ready");
  RTT::OperationCaller<std::string()> endpoint =
      opcua->getOperation("endpoint");
  BOOST_REQUIRE(ready.ready());
  BOOST_REQUIRE(endpoint.ready());
  BOOST_TEST(ready());
  BOOST_TEST(endpoint() == deployer.opcUaEndpoint());

  BOOST_REQUIRE(deployer.addPeer(&local));
  BOOST_REQUIRE(deployer.publishPeer(local.getName()));
  auto local_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpoint(), local.getName(), {}, &error);
  BOOST_REQUIRE_MESSAGE(local_proxy != nullptr, error);
  RTT::OperationCaller<std::int32_t(std::int32_t)> echo =
      local_proxy->getOperation("echo");
  BOOST_REQUIRE(echo.ready());
  BOOST_TEST(echo(42) == 42);

  BOOST_TEST(deployer.publishPeer(local.getName()));
  BOOST_REQUIRE(deployer.unpublishPeer(local.getName()));
  local_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpoint(), local.getName(), {}, &error);
  BOOST_TEST(local_proxy == nullptr);
  BOOST_TEST(!error.empty());
}

BOOST_AUTO_TEST_CASE(site_file_server_components_are_published) {
  loadCanonicalTypes();
  RTT::ComponentLoader::Instance()->addFactory("TestEchoTask", &createEchoTask);
  OCL::OpcUaDeploymentOptions options = deploymentOptions();
  TemporarySiteFile site_file(options.server.port);
  OCL::OpcUaDeploymentComponent deployer("Deployer", site_file.path().string(),
                                          options);

  std::string error;
  auto site_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpoint(), "SiteEcho", {}, &error);
  BOOST_REQUIRE_MESSAGE(site_proxy != nullptr, error);
  RTT::OperationCaller<std::int32_t(std::int32_t)> echo =
      site_proxy->getOperation("echo");
  BOOST_REQUIRE(echo.ready());
  BOOST_TEST(echo(84) == 84);
}

BOOST_AUTO_TEST_CASE(remote_components_are_owned_as_aliased_deployer_peers) {
  loadCanonicalTypes();

  RTT::opcua::ServerOptions remote_options;
  remote_options.port = unusedLoopbackPort();
  RTT::opcua::Server remote_server(remote_options);
  std::string error;
  BOOST_REQUIRE_MESSAGE(remote_server.start(&error), error);
  RTT::opcua::ObjectModel remote_model(remote_server);
  EchoTask remote("RemoteEcho");
  auto remote_registration = remote_model.registerComponent(remote, &error);
  BOOST_REQUIRE_MESSAGE(remote_registration.has_value(), error);

  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());
  auto deployer_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpoint(), deployer.getName(), {}, &error);
  BOOST_REQUIRE_MESSAGE(deployer_proxy != nullptr, error);
  RTT::Service::shared_ptr opcua = deployer_proxy->provides("opcua");
  BOOST_REQUIRE(opcua != nullptr);
  RTT::OperationCaller<bool(const std::string &, const std::string &,
                            const std::string &)>
      connect_remote = opcua->getOperation("connectRemote");
  RTT::OperationCaller<bool(const std::string &)> disconnect_remote =
      opcua->getOperation("disconnectRemote");
  BOOST_REQUIRE(connect_remote.ready());
  BOOST_REQUIRE(disconnect_remote.ready());
  BOOST_REQUIRE(connect_remote(remote_server.endpointUrl(), remote.getName(),
                               "RemoteAlias"));

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
  BOOST_REQUIRE(disconnect_remote("RemoteAlias"));
  BOOST_TEST(deployer.getPeer("RemoteAlias") == nullptr);
  BOOST_TEST(!deployer.disconnectRemote("RemoteAlias"));
  BOOST_TEST(!deployer.opcUaLastError().empty());
}
