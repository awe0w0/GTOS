#include "memory_record.h"

#include "third_party/expat/src/expat/lib/expat.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
int gtos_native_environment_initialize() noexcept;
}

namespace {

void Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
}

bool HasRdrand() {
  unsigned a, b, c, d;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0u), "c"(0u));
  if (a < 1) {
    return false;
  }
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1u), "c"(0u));
  return (c & (1u << 30)) != 0;
}

void XMLCALL Start(void* data, const XML_Char* name, const XML_Char**) {
  auto& events = *static_cast<unsigned*>(data);
  events = (events << 4) | (name[0] == 'r' ? 1u : 2u);
}

void XMLCALL End(void* data, const XML_Char* name) {
  auto& events = *static_cast<unsigned*>(data);
  events = (events << 4) | (name[0] == 'r' ? 3u : 4u);
}

template <unsigned Size>
unsigned Parse(const char (&xml)[Size], XML_Status status, XML_Error error,
               unsigned expected_events) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  Check(parser != nullptr);
  if (!parser) {
    return 0;
  }
  unsigned events = 0;
  XML_SetUserData(parser, &events);
  XML_SetElementHandler(parser, Start, End);
  Check(XML_Parse(parser, xml, Size - 1, XML_TRUE) == status);
  Check(XML_GetErrorCode(parser) == error);
  Check(events == expected_events);
  XML_ParserFree(parser);
  return events;
}

}  // namespace

extern "C" int NativeEntry() {
  Check(gtos_native_environment_initialize() == 0);
  const bool entropy = HasRdrand();
  native_memory_record.base = entropy;
  native_memory_record.fault_address = Parse(
      "<r><x/></r>", entropy ? XML_STATUS_OK : XML_STATUS_ERROR,
      entropy ? XML_ERROR_NONE : XML_ERROR_NO_MEMORY, entropy ? 0x1243 : 0);
#if defined(GTOS_EXPAT_GUEST_WRONG_EXPECTATION)
  const XML_Error bad_xml = XML_ERROR_SYNTAX;
#else
  const XML_Error bad_xml = XML_ERROR_TAG_MISMATCH;
#endif
  Parse("<r><x></r>", XML_STATUS_ERROR,
        entropy ? bad_xml : XML_ERROR_NO_MEMORY, entropy ? 0x12 : 0);
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}
