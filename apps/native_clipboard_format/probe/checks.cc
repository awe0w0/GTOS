#include "api.h"
#include <limits.h>
#include <string>
#include <string_view>

#include "ui/base/clipboard/clipboard_format_type.h"

namespace {
volatile NativeClipboardRecord* observation;

#define REQUIRE_CLIPBOARD(condition)                                  \
  do {                                                                \
    observation->checks = observation->checks + 1;                     \
    if (!(condition)) {                                               \
      observation->error = __LINE__;                                  \
      return __LINE__;                                                \
    }                                                                 \
  } while (false)

uint32_t FormatCases() {
  struct Case {
    const ui::ClipboardFormatType& (*get)();
    const char* expected;
  };
  constexpr Case cases[] = {
      {ui::ClipboardFormatType::FilenamesType, "text/uri-list"},
      {ui::ClipboardFormatType::UrlType, "text/x-moz-url"},
      {ui::ClipboardFormatType::PlainTextType, "text/plain"},
      {ui::ClipboardFormatType::HtmlType, "text/html"},
      {ui::ClipboardFormatType::SvgType, "image/svg+xml"},
      {ui::ClipboardFormatType::RtfType, "text/rtf"},
      {ui::ClipboardFormatType::PngType, "image/png"},
      {ui::ClipboardFormatType::BitmapType, "image/png"},
      {ui::ClipboardFormatType::WebKitSmartPasteType, "chromium/x-webkit-paste"},
      {ui::ClipboardFormatType::DataTransferCustomType,
       "chromium/x-web-custom-data"},
      {ui::ClipboardFormatType::BookmarkEntriesType,
       "chromium/x-bookmark-entries"},
      {ui::ClipboardFormatType::WebCustomFormatMap,
       "application/web;type=\"custom/formatmap\""}};
  for (const Case& test : cases) {
    const ui::ClipboardFormatType& type = test.get();
    REQUIRE_CLIPBOARD(type.Serialize() == test.expected &&
                      type.GetName() == test.expected && &type == &test.get());
    REQUIRE_CLIPBOARD(ui::ClipboardFormatType::Deserialize(test.expected) ==
                      type);
    observation->format_cases = observation->format_cases + 1;
  }
  REQUIRE_CLIPBOARD(&ui::ClipboardFormatType::PngType() ==
                    &ui::ClipboardFormatType::BitmapType());
  return 0;
}

uint32_t SerializationCases() {
  const ui::ClipboardFormatType empty;
  REQUIRE_CLIPBOARD(empty.Serialize().empty() && empty.GetName().empty() &&
                    empty == ui::ClipboardFormatType::Deserialize({}));
  observation->serialization_cases = observation->serialization_cases + 1;
  for (unsigned length : {0U, 1U, 7U, 15U, 22U, 23U, 24U, 31U, 255U, 4096U}) {
    std::string source(length, 'a');
    for (unsigned index = 0; index < length; ++index) {
      source[index] = static_cast<char>('a' + index % 26);
    }
    const auto type = ui::ClipboardFormatType::Deserialize(
        std::string_view(source.data(), source.size()));
    const std::string serialized = type.Serialize();
    REQUIRE_CLIPBOARD(serialized.size() == source.size() &&
                      serialized == source && type.GetName() == source);
    REQUIRE_CLIPBOARD(ui::ClipboardFormatType::Deserialize(serialized) == type);
    observation->serialization_cases = observation->serialization_cases + 1;
  }
  constexpr char nul[] = {'a', '\0', 'b', '\0', 'c'};
  constexpr char non_ascii[] = {'n', '-', '\xff', '\x80'};
  constexpr char mixed[] = {'\0', '\xe4', '\xbd', '\xa0', '\0', '\xff'};
  const std::string_view binary[] = {
      {nul, sizeof(nul)}, {non_ascii, sizeof(non_ascii)}, {mixed, sizeof(mixed)}};
  // Deserialize preserves opaque serialized bytes. Only CustomPlatformType
  // imposes the upstream ASCII CHECK.
  for (std::string_view source : binary) {
    const std::string expected(source);
    const auto type = ui::ClipboardFormatType::Deserialize(source);
    const std::string serialized = type.Serialize();
    REQUIRE_CLIPBOARD(serialized.size() == source.size() &&
                      serialized == expected && type.GetName() == expected);
    REQUIRE_CLIPBOARD(ui::ClipboardFormatType::Deserialize(serialized) == type);
    observation->serialization_cases = observation->serialization_cases + 1;
  }
  return 0;
}

uint32_t ComparisonCases() {
  // Ordered as unsigned bytes, including prefixes containing NUL.
  constexpr std::string_view values[] = {
      "", "a", {"a\0", 2}, {"a\0b", 3}, "aa", "ab", "b",
      {"\x7f", 1}, {"\x80", 1}, {"\xff", 1}};
  for (unsigned left = 0; left < 10; ++left) {
    const auto first = ui::ClipboardFormatType::Deserialize(values[left]);
    for (unsigned right = 0; right < 10; ++right) {
      const auto second = ui::ClipboardFormatType::Deserialize(values[right]);
      REQUIRE_CLIPBOARD((first < second) == (left < right) &&
                        (first == second) == (left == right));
      observation->comparison_cases = observation->comparison_cases + 1;
    }
  }
  return 0;
}

uint32_t LifetimeCases() {
  for (unsigned wave = 0; wave < 32; ++wave) {
    const unsigned length = (wave & 1) ? 2048 + wave : 7;
    const std::string expected(length, static_cast<char>('a' + wave % 26));
    ui::ClipboardFormatType survivor;
    ui::ClipboardFormatType copied;
    {
      std::string source(expected);
      auto original = ui::ClipboardFormatType::Deserialize(source);
      const ui::ClipboardFormatType constructed(original);
      survivor = original;
      copied = constructed;
      source.assign(length, '!');
      REQUIRE_CLIPBOARD(original.Serialize() == expected &&
                        constructed.Serialize() == expected &&
                        survivor.Serialize() == expected &&
                        copied.Serialize() == expected);
      original = ui::ClipboardFormatType::Deserialize("changed");
      source.clear();
      REQUIRE_CLIPBOARD(original.Serialize() == "changed" &&
                        constructed.Serialize() == expected &&
                        survivor.Serialize() == expected &&
                        copied.Serialize() == expected);
    }
    // The source string, original format, and copy-constructed format have
    // been destroyed. These assigned values must still own their bytes.
    REQUIRE_CLIPBOARD(survivor.Serialize() == expected &&
                      survivor.GetName() == expected &&
                      copied.Serialize() == expected && survivor == copied);
    std::string returned = survivor.Serialize();
    returned.assign(length, '?');
    REQUIRE_CLIPBOARD(survivor.Serialize() == expected &&
                      copied.Serialize() == expected);
    observation->retained_bytes = observation->retained_bytes + 2 * length;
    observation->lifetime_waves = observation->lifetime_waves + 1;
  }
  return 0;
}

uint32_t CustomCases() {
  const std::string long_name(257, 'x');
  const std::string_view cases[] = {
      "", "text/plain", "application/x-gtos-native", long_name};
  for (std::string_view source : cases) {
    const auto type = ui::ClipboardFormatType::CustomPlatformType(source);
    REQUIRE_CLIPBOARD(type.Serialize() == source && type.GetName() == source &&
                      type == ui::ClipboardFormatType::Deserialize(source));
    observation->custom_cases = observation->custom_cases + 1;
  }
  return 0;
}

uint32_t WebCustomCases() {
  struct Case { int index; const char* expected; };
  constexpr Case cases[] = {
      {0, "application/web;type=\"custom/format0\""},
      {-1, "application/web;type=\"custom/format-1\""},
      {INT_MIN, "application/web;type=\"custom/format-2147483648\""},
      {INT_MAX, "application/web;type=\"custom/format2147483647\""}};
  for (const Case& test : cases) {
    const std::string name = ui::ClipboardFormatType::WebCustomFormatName(
        test.index);
    REQUIRE_CLIPBOARD(name == test.expected &&
                      ui::ClipboardFormatType::WebCustomFormatName(test.index)
                          == name);
    observation->web_custom_cases = observation->web_custom_cases + 1;
  }
  return 0;
}
}  // namespace

extern "C" uint32_t NativeClipboardChecks(
    volatile NativeClipboardRecord* record) {
  observation = record;
  if (record->mode == 1) {
    constexpr char non_ascii_format[] = "non-ascii-\xff";
    observation->result = 0xffffffffU;
    observation->ascii_rejection_attempts =
        observation->ascii_rejection_attempts + 1;
    observation->stage = 10;
    // The unmodified upstream CHECK must terminate this task. A returned
    // value is a failure, never evidence that rejection worked.
    const auto rejected = ui::ClipboardFormatType::CustomPlatformType(
        std::string_view(non_ascii_format, sizeof(non_ascii_format) - 1));
    (void)rejected;
    observation->stage = 11;
    const uint32_t result = __LINE__;
    observation->error = result;
    return result;
  }
  REQUIRE_CLIPBOARD(record->mode == 0);
  observation->stage = 4;
  const uint32_t format_result = FormatCases();
  if (format_result) return format_result;
  observation->mask = observation->mask | 1;
  observation->stage = 5;
  const uint32_t serialization_result = SerializationCases();
  if (serialization_result) return serialization_result;
  observation->mask = observation->mask | 2;
  observation->stage = 6;
  const uint32_t comparison_result = ComparisonCases();
  if (comparison_result) return comparison_result;
  observation->mask = observation->mask | 4;
  observation->stage = 7;
  const uint32_t lifetime_result = LifetimeCases();
  if (lifetime_result) return lifetime_result;
  observation->mask = observation->mask | 8;
  observation->stage = 8;
  const uint32_t custom_result = CustomCases();
  if (custom_result) return custom_result;
  observation->mask = observation->mask | 16;
  observation->stage = 9;
  const uint32_t web_result = WebCustomCases();
  if (web_result) return web_result;
  observation->mask = observation->mask | 32;
  return 0;
}
