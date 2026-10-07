#ifndef GTOS_PROCESS_RESOURCES_H
#define GTOS_PROCESS_RESOURCES_H
#include <process/resource_abi.h>

namespace gtos { namespace process {
    // Kernel-only plan. Its source is immutable kernel rodata, never an ABI field.
    struct ResourceReadPlan {
        const unsigned char* source;
        unsigned bytes;
    };
    // Pure bounded catalog operations; failure leaves the output unchanged.
    int ResourceInfo(unsigned id, GtosResourceInfo& output);
    int ResourcePlan(const GtosResourceReadRequest&, unsigned requestBytes,
                     ResourceReadPlan& output);
} }
#endif
