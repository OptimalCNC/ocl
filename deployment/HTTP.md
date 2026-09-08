# Component-owned HTTP service

Build OCL with `BUILD_HTTP=ON` and an installed `rtt_http` SDK to provide
`loadService("Deployer", "http")`. The service uses DeploymentComponent's
attachment, unload-veto, and two-phase shutdown mechanisms. It is independent
of the optional OPC UA service and has no `deployer-http` executable.

Import application typekits and separate codec transports before starting:

```text
import("rtt_http")
import("ocl")
loadService("Deployer", "http")
http.start()
http.publishComponent("arm")
```

`arm` must already be a local application component. Global creation, ordinary
TaskContext owners, duplicates, Deployer publication, and managed remote proxies
are rejected. Local service properties configure the listener only while Stopped.
Direct assignment and RTT reference/update assignment both enforce that guard.

Ordinary stop/restart keeps static publication, bridges, and unfinished calls.
A published component cannot be unloaded individually. Final deployment shutdown
closes all services before any participant drains operations; HTTP network cleanup
progresses independently while another participant is still draining. Component
engines and storage remain alive through completion. No service unload API is added.

The SDK owns JSON conversion, REST routing, network workers, and operation capacity.
See [rtt_http](https://github.com/liufang-robot/rtt_http) for its native and wire APIs.
The application must ensure safe concurrent RTT property/attribute access.
