#include "deployment/DeploymentComponent.hpp"

#include <string>

int main()
{
    typedef bool (OCL::DeploymentComponent::*SetPeriodicActivityOnCPU)(
        const std::string&, double, int, int, unsigned int);

    SetPeriodicActivityOnCPU method =
        &OCL::DeploymentComponent::setPeriodicActivityOnCPU;

    return method ? 0 : 1;
}
