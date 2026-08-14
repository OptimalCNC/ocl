#define BOOST_TEST_MODULE ocl_opcua_deployment
#include <boost/test/included/unit_test.hpp>

#include "deployment/OpcUaDeploymentComponent.hpp"

#include <rtt/InputPort.hpp>
#include <rtt/OperationCaller.hpp>
#include <rtt/OutputPort.hpp>
#include <rtt/Property.hpp>
#include <rtt/Service.hpp>
#include <rtt/TaskContext.hpp>
#include <rtt/base/AttributeBase.hpp>
#include <rtt/base/InputPortInterface.hpp>
#include <rtt/base/OutputPortInterface.hpp>
#include <rtt/deployment/ComponentLoader.hpp>
#include <rtt/internal/DataSources.hpp>
#include <rtt/opcua/port_direction.hpp>
#include <rtt/opcua/datatype_registry.hpp>
#include <rtt/opcua/node_id.hpp>
#include <rtt/opcua/object_model.hpp>
#include <rtt/opcua/server.hpp>
#include <rtt/opcua/task_context_proxy.hpp>
#include <rtt/os/StartStopManager.hpp>
#include <rtt/os/startstop.h>
#include <rtt/typekit/RealTimeTypekit.hpp>
#include <rtt/types/TemplateTypeInfo.hpp>
#include <rtt/types/Types.hpp>

#include <open62541pp/client.hpp>
#include <open62541pp/services/attribute_highlevel.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

class RttProcessFixture final {
public:
  RttProcessFixture() {
    auto &suite = boost::unit_test::framework::master_test_suite();
    if (__os_init(suite.argc, suite.argv) != 0) {
      throw std::runtime_error("failed to initialize RTT test process");
    }
  }

  ~RttProcessFixture() {
#ifdef OROCOS_TARGET_XENOMAI
    RTT::os::StartStopManager::Instance()->stop();
    RTT::os::StartStopManager::Release();
#else
    __os_exit();
#endif
  }

  RttProcessFixture(const RttProcessFixture &) = delete;
  RttProcessFixture &operator=(const RttProcessFixture &) = delete;
};

BOOST_GLOBAL_FIXTURE(RttProcessFixture);

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

::opcua::NodeId modelNodeId(std::uint16_t namespace_index,
                            std::initializer_list<std::string_view> segments) {
  const std::vector<std::string_view> path_segments(segments);
  return ::opcua::NodeId(namespace_index,
                         RTT::opcua::makeNodePath(path_segments));
}

std::uint16_t namespaceIndex(::opcua::Client &client) {
  const auto namespaces = client.namespaceArray();
  const auto found = std::find(namespaces.begin(), namespaces.end(),
                               RTT::opcua::kNamespaceUri);
  if (found == namespaces.end()) {
    throw std::runtime_error("RTT OPC UA namespace URI is missing");
  }
  const auto index = std::distance(namespaces.begin(), found);
  if (index < 0 || index > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("RTT OPC UA namespace index is invalid");
  }
  return static_cast<std::uint16_t>(index);
}

void requireMissingNode(::opcua::Client &client, const ::opcua::NodeId &id) {
  const auto result = ::opcua::services::readNodeClass(client, id);
  BOOST_REQUIRE(!result);
  BOOST_TEST(result.code() == UA_STATUSCODE_BADNODEIDUNKNOWN);
}

void requirePortDirection(
    ::opcua::Client &client, std::uint16_t namespace_index,
    std::initializer_list<std::string_view> segments,
    RTT::opcua::PortDirection expected) {
  const auto id = modelNodeId(namespace_index, segments);
  const auto value = ::opcua::services::readValue(client, id);
  BOOST_REQUIRE(value);
  BOOST_TEST(value.value().isScalar());
  BOOST_TEST(value.value().isType(
      ::opcua::NodeId(::opcua::DataTypeId::Int32)));
  BOOST_TEST(value.value().to<std::int32_t>() ==
             static_cast<std::int32_t>(expected));
}

template <typename Predicate>
bool waitUntil(Predicate predicate,
               std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
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

class CompleteMappingTask final : public RTT::TaskContext {
public:
  CompleteMappingTask()
      : RTT::TaskContext("CompleteMapping"),
        control(RTT::Service::Create("control")) {
    addOperation("add", &CompleteMappingTask::add, this, RTT::ClientThread)
        .arg("left", "Left operand.")
        .arg("right", "Right operand.");
    addProperty("Gain", gain);
    addAttribute("Mode", mode);
    addConstant("Model", model);
    addPort(command);
    addEventPort(trigger);
    addPort(feedback);

    control->addOperation("scale", &CompleteMappingTask::scale, this,
                          RTT::ClientThread)
        .arg("value", "Value to scale.");
    control->addProperty("Offset", offset);
    control->addAttribute("Enabled", enabled);
    control->addConstant("Unit", unit);
    control->addPort(service_command);
    control->addPort(service_feedback);
    BOOST_REQUIRE(provides()->addService(control));
  }

  ~CompleteMappingTask() override {
    control->removePort(service_command.getName());
    control->removePort(service_feedback.getName());
    control->clear();
  }

  std::int32_t add(std::int32_t left, std::int32_t right) const {
    return left + right;
  }

  std::int32_t scale(std::int32_t value) const { return value * 2 + offset; }

  std::int32_t gain{7};
  std::string mode{"manual"};
  std::string model{"fixture-v1"};
  RTT::InputPort<std::int32_t> command{"Command"};
  RTT::InputPort<bool> trigger{"Trigger"};
  RTT::OutputPort<std::int32_t> feedback{"Feedback"};
  RTT::Service::shared_ptr control;
  std::int32_t offset{2};
  bool enabled{false};
  std::string unit{"counts"};
  RTT::InputPort<std::int32_t> service_command{"ServiceCommand"};
  RTT::OutputPort<std::int32_t> service_feedback{"ServiceFeedback"};
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
  CompleteMappingTask complete;
  UnsupportedTask unsupported;
  OCL::OpcUaDeploymentComponent deployer("Deployer", "", deploymentOptions());
  BOOST_REQUIRE(deployer.addPeer(&local));
  BOOST_REQUIRE(deployer.addPeer(&complete));
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

  BOOST_REQUIRE(deployer.publishComponent(complete.getName()));
  ::opcua::Client client;
  client.connect(deployer.opcUaEndpointUrl());
  const std::uint16_t namespace_index = namespaceIndex(client);
  requirePortDirection(
      client, namespace_index,
      {"components", "CompleteMapping", "ports", "Command", "direction"},
      RTT::opcua::PortDirection::input);
  requirePortDirection(
      client, namespace_index,
      {"components", "CompleteMapping", "ports", "Trigger", "direction"},
      RTT::opcua::PortDirection::input);
  requirePortDirection(
      client, namespace_index,
      {"components", "CompleteMapping", "ports", "Feedback", "direction"},
      RTT::opcua::PortDirection::output);
  requirePortDirection(
      client, namespace_index,
      {"components", "CompleteMapping", "services", "control", "ports",
       "ServiceCommand", "direction"},
      RTT::opcua::PortDirection::input);
  requirePortDirection(
      client, namespace_index,
      {"components", "CompleteMapping", "services", "control", "ports",
       "ServiceFeedback", "direction"},
      RTT::opcua::PortDirection::output);
  BOOST_REQUIRE(::opcua::services::readNodeClass(
      client,
      modelNodeId(namespace_index, {"components", "CompleteMapping", "services",
                                    "Command", "operations"})));
  for (const std::string_view category :
       {"properties", "attributes", "ports", "services"}) {
    requireMissingNode(client, modelNodeId(namespace_index,
                                           {"components", "CompleteMapping",
                                            "services", "Command", category}));
  }
  client.disconnect();

  auto complete_proxy = RTT::opcua::TaskContextProxy::create(
      deployer.opcUaEndpointUrl(), complete.getName(), {}, &error);
  BOOST_REQUIRE_MESSAGE(complete_proxy != nullptr, error);

  RTT::OperationCaller<std::int32_t(std::int32_t, std::int32_t)> add =
      complete_proxy->getOperation("add");
  BOOST_REQUIRE(add.ready());
  BOOST_TEST(add(20, 22) == 42);
  auto *complete_gain = dynamic_cast<RTT::Property<std::int32_t> *>(
      complete_proxy->provides()->getProperty("Gain"));
  BOOST_REQUIRE(complete_gain != nullptr);
  complete_gain->set(11);
  BOOST_TEST(complete.gain == 11);
  RTT::base::AttributeBase *mode =
      complete_proxy->provides()->getAttribute("Mode");
  BOOST_REQUIRE(mode != nullptr);
  auto *mode_source = RTT::internal::AssignableDataSource<std::string>::narrow(
      mode->getDataSource().get());
  BOOST_REQUIRE(mode_source != nullptr);
  mode_source->set("automatic");
  BOOST_TEST(complete.mode == "automatic");
  RTT::base::AttributeBase *model =
      complete_proxy->provides()->getAttribute("Model");
  BOOST_REQUIRE(model != nullptr);
  BOOST_TEST(!model->getDataSource()->isAssignable());
  auto *model_source = RTT::internal::DataSource<std::string>::narrow(
      model->getDataSource().get());
  BOOST_REQUIRE(model_source != nullptr);
  BOOST_TEST(model_source->get() == "fixture-v1");

  auto *remote_command = dynamic_cast<RTT::base::InputPortInterface *>(
      complete_proxy->ports()->getPort("Command"));
  auto *remote_feedback = dynamic_cast<RTT::base::OutputPortInterface *>(
      complete_proxy->ports()->getPort("Feedback"));
  BOOST_REQUIRE(remote_command != nullptr);
  BOOST_REQUIRE(remote_feedback != nullptr);
  BOOST_REQUIRE(complete_proxy->ports()->getPort("Trigger") != nullptr);
  RTT::Service::shared_ptr command_service =
      complete_proxy->provides()->getService("Command");
  RTT::Service::shared_ptr trigger_service =
      complete_proxy->provides()->getService("Trigger");
  RTT::Service::shared_ptr feedback_service =
      complete_proxy->provides()->getService("Feedback");
  BOOST_REQUIRE(command_service);
  BOOST_REQUIRE(trigger_service);
  BOOST_REQUIRE(feedback_service);
  BOOST_REQUIRE(command_service->getOperation("read") != nullptr);
  BOOST_REQUIRE(trigger_service->getOperation("read") != nullptr);
  BOOST_REQUIRE(feedback_service->getOperation("last") != nullptr);

  RTT::OutputPort<std::int32_t> command_source("CommandSource");
  BOOST_REQUIRE(command_source.createConnection(
      *remote_command,
      RTT::ConnPolicy::data(RTT::ConnPolicy::LOCK_FREE, false)));
  BOOST_TEST(command_source.write(std::int32_t{73}) == RTT::WriteSuccess);
  std::int32_t command_value = 0;
  BOOST_REQUIRE(waitUntil(
      [&] { return complete.command.read(command_value) == RTT::NewData; }));
  BOOST_TEST(command_value == 73);

  RTT::InputPort<std::int32_t> feedback_sink("FeedbackSink");
  BOOST_REQUIRE(remote_feedback->createConnection(
      feedback_sink, RTT::ConnPolicy::data(RTT::ConnPolicy::LOCK_FREE, false)));
  BOOST_TEST(complete.feedback.write(std::int32_t{84}) == RTT::WriteSuccess);
  std::int32_t feedback_value = 0;
  BOOST_REQUIRE(waitUntil(
      [&] { return feedback_sink.read(feedback_value) == RTT::NewData; }));
  BOOST_TEST(feedback_value == 84);
  RTT::OperationCaller<std::int32_t()> last =
      feedback_service->getOperation("last");
  BOOST_REQUIRE(last.ready());
  BOOST_TEST(last() == 84);

  RTT::Service::shared_ptr control =
      complete_proxy->provides()->getService("control");
  BOOST_REQUIRE(control);
  RTT::OperationCaller<std::int32_t(std::int32_t)> scale =
      control->getOperation("scale");
  BOOST_REQUIRE(scale.ready());
  auto *offset = dynamic_cast<RTT::Property<std::int32_t> *>(
      control->getProperty("Offset"));
  BOOST_REQUIRE(offset != nullptr);
  offset->set(3);
  BOOST_TEST(complete.offset == 3);
  BOOST_TEST(scale(10) == 23);
  RTT::base::AttributeBase *enabled = control->getAttribute("Enabled");
  BOOST_REQUIRE(enabled != nullptr);
  auto *enabled_source = RTT::internal::AssignableDataSource<bool>::narrow(
      enabled->getDataSource().get());
  BOOST_REQUIRE(enabled_source != nullptr);
  enabled_source->set(true);
  BOOST_TEST(complete.enabled);
  RTT::base::AttributeBase *unit = control->getAttribute("Unit");
  BOOST_REQUIRE(unit != nullptr);
  BOOST_TEST(!unit->getDataSource()->isAssignable());
  BOOST_REQUIRE(control->getPort("ServiceCommand") != nullptr);
  auto *remote_service_feedback =
      dynamic_cast<RTT::base::OutputPortInterface *>(
          control->getPort("ServiceFeedback"));
  BOOST_REQUIRE(remote_service_feedback != nullptr);
  BOOST_REQUIRE(control->getService("ServiceCommand"));
  BOOST_REQUIRE(control->getService("ServiceFeedback"));
  RTT::InputPort<std::int32_t> service_feedback_sink("ServiceFeedbackSink");
  BOOST_REQUIRE(remote_service_feedback->createConnection(
      service_feedback_sink,
      RTT::ConnPolicy::data(RTT::ConnPolicy::LOCK_FREE, false)));
  BOOST_TEST(complete.service_feedback.write(std::int32_t{91}) ==
             RTT::WriteSuccess);
  std::int32_t service_feedback_value = 0;
  BOOST_REQUIRE(waitUntil([&] {
    return service_feedback_sink.read(service_feedback_value) == RTT::NewData;
  }));
  BOOST_TEST(service_feedback_value == 91);

  service_feedback_sink.disconnect();
  feedback_sink.disconnect();
  command_source.disconnect();

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
