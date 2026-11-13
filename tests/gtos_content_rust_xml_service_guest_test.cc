// Checks the actual Chromium data decoder Rust/C++ DOM contract.
#include <array>
#include <string>
#include <string_view>
#include <process/abi.h>

#include "base/at_exit.h"
#include "services/data_decoder/public/cpp/xml_dom.h"

namespace {
using data_decoder::xml::Document;
using data_decoder::xml::Node;

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

bool GoodDom() {
  auto doc = Document::FromUtf8(
      "<gtos xmlns:p='urn:example'><p:item id='7'>text</p:item>"
      "<item><![CDATA[<xml>]]></item></gtos>");
  if (!doc.has_value()) {
    return false;
  }
  const auto* root = doc->GetRoot();
  const auto* first = doc->FindFirstElementByTagName({"item", "p"});
  const auto* second = doc->FindFirstElementByTagName({"item", ""});
  if (!root || !first || !second || first->parent() != root ||
      second->parent() != root || !root->GetLocalName() ||
      *root->GetLocalName() != "gtos") {
    return false;
  }
  const auto* id = first->GetAttribute({"id", ""});
  const auto* text = first->GetChildren();
  const auto* cdata = second->GetChildren();
  return id && *id == "7" && text && text->size() == 1 &&
         text->front()->GetType() == Node::Type::kText &&
         text->front()->GetTextContent() == "text" &&
         cdata && cdata->size() == 1 &&
         cdata->front()->GetType() == Node::Type::kCdata &&
         cdata->front()->GetTextContent() == "<xml>";
}

bool BadInputsRejected() {
  const auto mismatch = Document::FromUtf8("<gtos><item></gtos>");
  if (mismatch.has_value() || mismatch.error().empty()) {
    return false;
  }
  constexpr std::array<uint8_t, 8> bad_utf8{'<', 'x', '>', 0xff,
                                             '<', '/', 'x', '>'};
  if (Document::FromBytes(bad_utf8).has_value()) {
    return false;
  }
  std::string deep;
  for (int i = 0; i < 201; ++i) {
    deep += "<x>";
  }
  for (int i = 0; i < 201; ++i) {
    deep += "</x>";
  }
  return !Document::FromUtf8(deep).has_value();
}
}  // namespace

extern "C" int GtosContentShellMain(int, const char**) {
  base::AtExitManager at_exit;
  if (!GoodDom() || !Say("GTOS CHROMIUM RUST XML DOM PASS\n")) {
    return 253;
  }
  if (!BadInputsRejected() || !Say("GTOS CHROMIUM RUST XML REJECTION PASS\n")) {
    return 254;
  }
  return 37;
}
