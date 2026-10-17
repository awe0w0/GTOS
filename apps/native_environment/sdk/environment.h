#ifndef GTOS_NATIVE_ENVIRONMENT_H
#define GTOS_NATIVE_ENVIRONMENT_H
#include <process/environment_abi.h>
namespace gtos_native {
    bool ValidEnvironment(const GtosEnvironmentReadResult&);
}
extern "C" int gtos_native_environment_initialize() noexcept;
#endif
