#include <i18n/catalog.h>
using namespace gtos::i18n;
namespace {
struct Entry {
    const char *english;
    const char *chinese;
};
const Entry catalog[] = {
#define GTOS_STRING(id, en, zh) {en, zh},
#include <i18n/catalog_data.inc>
#undef GTOS_STRING
};
bool Equal(const char *a, const char *b) {
    if (!a || !b)
        return false;
    for (uint32_t i = 0; i < 4096; ++i) {
        if (a[i] != b[i])
            return false;
        if (!a[i])
            return true;
    }
    return false;
}
} // namespace
const char *gtos::i18n::Text(Locale locale, StringId id) {
    if ((uint32_t)id >= StringCount)
        id = UnknownStatus;
    return locale == SimplifiedChinese ? catalog[id].chinese : catalog[id].english;
}
bool gtos::i18n::FindStringId(const char *english, StringId &result) {
    for (uint32_t i = 0; i < StringCount; ++i)
        if (Equal(english, catalog[i].english)) {
            result = (StringId)i;
            return true;
        }
    return false;
}
const char *gtos::i18n::Translate(Locale locale, const char *english) {
    StringId id;
    return Text(locale, FindStringId(english, id) ? id : UnknownStatus);
}
const char *gtos::i18n::LocaleTag(Locale locale) {
    return locale == SimplifiedChinese ? "zh-CN" : "en";
}
