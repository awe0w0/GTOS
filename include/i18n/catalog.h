#ifndef GTOS_I18N_CATALOG_H
#define GTOS_I18N_CATALOG_H
#include <common/types.h>
namespace gtos {
namespace i18n {
enum Locale : uint32_t { English = 0, SimplifiedChinese = 1 };
enum StringId : uint32_t {
#define GTOS_STRING(id, en, zh) id,
#include <i18n/catalog_data.inc>
#undef GTOS_STRING
    StringCount
};
const char *Text(Locale locale, StringId id);
// Exact matching bridge for existing store/VM status messages, never for user content.
// An unknown message yields localized UnknownStatus, not an English substitution.
const char *Translate(Locale locale, const char *english);
bool FindStringId(const char *english, StringId &result);
const char *LocaleTag(Locale locale);
} // namespace i18n
} // namespace gtos
#endif
