// GTOS native input/cancellation does not expose POSIX signal handlers.
#include "perfetto/ext/base/ctrl_c_handler.h"
#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS)
#error The native Ctrl-C policy requires the GTOS platform
#endif

namespace perfetto {
namespace base {

void InstallCtrlCHandler(CtrlCHandlerFunction) {
  // This void API has no failure return. A requested unsupported service must
  // fail explicitly rather than leave its caller expecting installed handlers.
  PERFETTO_ELOG("GTOS does not expose POSIX signal handlers");
  PERFETTO_IMMEDIATE_CRASH();
}

}  // namespace base
}  // namespace perfetto
