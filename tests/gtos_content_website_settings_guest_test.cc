#include <process/abi.h>

#include "base/at_exit.h"
#include "base/values.h"
#include "components/content_settings/core/browser/website_settings_registry.h"

namespace {
using content_settings::WebsiteSettingsInfo;
using content_settings::WebsiteSettingsRegistry;

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

const WebsiteSettingsInfo* Register(
    WebsiteSettingsRegistry* registry,
    ContentSettingsType type,
    WebsiteSettingsRegistry::Platforms mask) {
  return registry->Register(type, "gtos-test", base::Value(91),
      WebsiteSettingsInfo::UNSYNCABLE, WebsiteSettingsInfo::NOT_LOSSY,
      WebsiteSettingsInfo::TOP_ORIGIN_ONLY_SCOPE, mask,
      WebsiteSettingsInfo::DONT_INHERIT_IN_INCOGNITO);
}
}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
  base::AtExitManager at_exit;
  auto* registry = WebsiteSettingsRegistry::GetInstance();
  if (!registry->Get(ContentSettingsType::AUTO_SELECT_CERTIFICATE) ||
      !registry->Get(ContentSettingsType::SSL_CERT_DECISIONS)) {
    return 201;
  }
  constexpr auto type = static_cast<ContentSettingsType>(10000);
  const auto* accepted =
      Register(registry, type, WebsiteSettingsRegistry::PLATFORM_GTOS);
  if (!accepted || registry->Get(type) != accepted ||
      registry->GetByName("gtos-test") != accepted ||
      accepted->initial_default_value().GetInt() != 91 ||
      accepted->pref_name() != "profile.content_settings.exceptions.gtos_test") {
    return 202;
  }
  if (!Say("GTOS WEBSITE SETTINGS DEFAULTS AND PLATFORM PASS\n")) {
    return 203;
  }
  constexpr auto rejected = static_cast<ContentSettingsType>(10001);
  if (Register(registry, rejected, 0) ||
      Register(registry, rejected, WebsiteSettingsRegistry::PLATFORM_LINUX) ||
      Register(registry, rejected, WebsiteSettingsRegistry::PLATFORM_IOS) ||
      registry->Get(rejected) ||
      Register(registry, type, WebsiteSettingsRegistry::PLATFORM_CHROMEOS) ||
      registry->Get(type) != accepted) {
    return 204;
  }
  return Say("GTOS WEBSITE SETTINGS FOREIGN MASKS REJECTED\n") ? 37 : 205;
}

