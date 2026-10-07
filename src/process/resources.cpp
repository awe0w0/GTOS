#include <process/resources.h>

namespace {
    // Explicitly packed public PNG. The kernel's existing readonly region
    // includes this static array; no registration, host path or mutable source.
    const unsigned char PublicPng[] = {
#include "resources_png.inc"
    };
    static_assert(sizeof(PublicPng) == 1108U, "Explicit public PNG resource size");
    const unsigned PublicPngBytes = sizeof(PublicPng);
}

namespace gtos { namespace process {
    int ResourceInfo(unsigned id, GtosResourceInfo& output) {
        if (id != GTOS_RESOURCE_PUBLIC_PNG_ID) return GTOS_RESOURCE_ERR_NOT_FOUND;
        const GtosResourceInfo info = {
            GTOS_RESOURCE_ABI_VERSION, GTOS_RESOURCE_TYPE_PNG, PublicPngBytes,
            GTOS_RESOURCE_FLAG_READONLY | GTOS_RESOURCE_FLAG_PUBLIC
        };
        output = info;
        return 0;
    }

    int ResourcePlan(const GtosResourceReadRequest& request, unsigned requestBytes,
                     ResourceReadPlan& output) {
        if (requestBytes != GTOS_RESOURCE_READ_REQUEST_BYTES) return GTOS_RESOURCE_ERR_BAD_SIZE;
        if (request.version != GTOS_RESOURCE_ABI_VERSION) return GTOS_RESOURCE_ERR_UNSUPPORTED_VERSION;
        if (request.id != GTOS_RESOURCE_PUBLIC_PNG_ID) return GTOS_RESOURCE_ERR_NOT_FOUND;
        if (request.length > GTOS_RESOURCE_READ_LIMIT) return GTOS_RESOURCE_ERR_TOO_LARGE;
        if (request.offset > PublicPngBytes) return GTOS_RESOURCE_ERR_RANGE;
        const unsigned available = PublicPngBytes - request.offset;
        const unsigned bytes = request.length < available ? request.length : available;
        const ResourceReadPlan plan = {PublicPng + request.offset, bytes};
        output = plan;
        return static_cast<int>(bytes);
    }
} }
