#ifndef OCL_DEPLOYMENTSERVICELIFECYCLE_HPP
#define OCL_DEPLOYMENTSERVICELIFECYCLE_HPP

#include <ocl/OCL.hpp>

namespace RTT {
class TaskContext;
}

namespace OCL {

// Component-owned services participate in deployment lifetime, not RTT state
// hooks.
class OCL_API DeploymentServiceLifecycle {
public:
  virtual ~DeploymentServiceLifecycle() = default;
  virtual bool componentLoaded(RTT::TaskContext *component) = 0;
  virtual bool componentCanUnload(RTT::TaskContext *component) = 0;
  virtual void componentUnloaded(RTT::TaskContext *component) = 0;
  virtual void beginDeploymentShutdown() noexcept = 0;
  virtual void finishDeploymentShutdown() noexcept = 0;
};

} // namespace OCL

#endif
