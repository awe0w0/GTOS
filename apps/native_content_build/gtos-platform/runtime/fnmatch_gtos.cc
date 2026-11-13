// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fnmatch.h>

#include <string_view>

namespace {

unsigned char Fold(unsigned char byte, int flags) {
  if ((flags & FNM_CASEFOLD) && byte >= 'A' && byte <= 'Z') {
    return byte + ('a' - 'A');
  }
  return byte;
}

bool InClass(std::string_view name, unsigned char byte) {
  // The native SDK currently supplies the C locale.
  const bool digit = byte >= '0' && byte <= '9';
  const bool lower = byte >= 'a' && byte <= 'z';
  const bool upper = byte >= 'A' && byte <= 'Z';
  const bool alpha = lower || upper;
  const bool graph = byte >= '!' && byte <= '~';
  const auto folded = Fold(byte, FNM_CASEFOLD);
  if (name == "alnum") return alpha || digit;
  if (name == "alpha") return alpha;
  if (name == "blank") return byte == ' ' || byte == '\t';
  if (name == "cntrl") return byte < ' ' || byte == 127;
  if (name == "digit") return digit;
  if (name == "graph") return graph;
  if (name == "lower") return lower;
  if (name == "print") return graph || byte == ' ';
  if (name == "punct") return graph && !alpha && !digit;
  if (name == "space") return byte == ' ' || (byte >= '\t' && byte <= '\r');
  if (name == "upper") return upper;
  if (name == "xdigit") return digit || (folded >= 'a' && folded <= 'f');
  return false;
}

struct Atom {
  size_t width;
  unsigned char value;
  bool character;
  bool matches;
};

Atom ReadAtom(std::string_view pattern, unsigned char byte, int flags) {
  if (pattern.size() >= 3 && pattern.front() == '[') {
    const char type = pattern[1];
    if (type == ':' || type == '.' || type == '=') {
      for (size_t end = 2; end + 1 < pattern.size(); ++end) {
        if (pattern[end] != type || pattern[end + 1] != ']') {
          continue;
        }
        const auto item = pattern.substr(2, end - 2);
        if (type == ':') {
          return {end + 2, 0, false, InClass(item, byte)};
        }
        // In the C locale a collating symbol or equivalence class is one byte.
        if (item.size() == 1) {
          const auto value = static_cast<unsigned char>(item.front());
          return {end + 2, value, true, value == byte};
        }
        return {end + 2, 0, false, false};
      }
    }
  }
  size_t width = 1;
  if (!(flags & FNM_NOESCAPE) && pattern.front() == '\\' && pattern.size() > 1) {
    ++width;
  }
  const auto value = Fold(pattern[width - 1], flags);
  return {width, value, true, value == Fold(byte, flags)};
}

struct Bracket {
  size_t width;
  bool matches;
};

Bracket ReadBracket(std::string_view pattern, unsigned char byte, int flags) {
  size_t next = 1;
  const bool negate = next < pattern.size() &&
                      (pattern[next] == '!' || pattern[next] == '^');
  if (negate) {
    ++next;
  }
  const size_t first = next;
  bool matches = false;
  while (next < pattern.size()) {
    if (pattern[next] == ']' && next != first) {
      return {next + 1, negate != matches};
    }
    const Atom start = ReadAtom(pattern.substr(next), byte, flags);
    next += start.width;
    if (start.character && next + 1 < pattern.size() && pattern[next] == '-' &&
        pattern[next + 1] != ']') {
      const Atom end = ReadAtom(pattern.substr(next + 1), byte, flags);
      if (end.character) {
        const auto value = Fold(byte, flags);
        matches |= value >= start.value && value <= end.value;
        next += end.width + 1;
        continue;
      }
    }
    matches |= start.matches;
  }
  return {0, false};
}

bool Match(std::string_view pattern, std::string_view name, int flags) {
  size_t cursor = 0;
  size_t input = 0;
  size_t after_star = std::string_view::npos;
  size_t star_end = 0;
  const auto can_consume = [name, flags](size_t at) {
    return at < name.size() && (!(flags & FNM_PATHNAME) || name[at] != '/') &&
           (!(flags & FNM_PERIOD) || name[at] != '.' ||
            (at != 0 && (!(flags & FNM_PATHNAME) || name[at - 1] != '/')));
  };
  while (input < name.size()) {
    if (cursor < pattern.size()) {
      const char token = pattern[cursor];
      const auto byte = static_cast<unsigned char>(name[input]);
      const bool wildcard = can_consume(input);
      if (token == '*') {
        // A leading period requires a literal '.', even for a zero-width star.
        if (wildcard || ((flags & FNM_PATHNAME) && byte == '/')) {
          after_star = ++cursor;
          star_end = input;
          continue;
        }
      } else {
        size_t width = 1;
        bool matches = false;
        if (token == '?') {
          matches = wildcard;
        } else {
          const auto group = token == '['
                                 ? ReadBracket(pattern.substr(cursor), byte, flags)
                                 : Bracket{0, false};
          if (group.width) {
            width = group.width;
            matches = wildcard && group.matches;
          } else if (token == '\\' && !(flags & FNM_NOESCAPE)) {
            width = 2;
            matches = cursor + 1 < pattern.size() &&
                      Fold(pattern[cursor + 1], flags) == Fold(byte, flags);
          } else {
            matches = Fold(token, flags) == Fold(byte, flags);
          }
        }
        if (matches) {
          cursor += width;
          ++input;
          continue;
        }
      }
    }
    if (after_star == std::string_view::npos || !can_consume(star_end)) {
      return false;
    }
    cursor = after_star;
    input = ++star_end;
  }
  while (cursor < pattern.size() && pattern[cursor] == '*') {
    ++cursor;
  }
  return cursor == pattern.size();
}

}  // namespace

extern "C" int fnmatch(const char* pattern, const char* name, int flags) {
  return Match(pattern, name, flags) ? 0 : FNM_NOMATCH;
}
