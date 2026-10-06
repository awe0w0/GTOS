#include <i18n/catalog.h>
#include <i18n/font.h>
#include <i18n/pinyin.h>
using namespace gtos::i18n;
static int failures = 0;
static void Check(bool value) {
    if (!value)
        ++failures;
}
static bool Equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static void Type(PinyinComposer &p, const char *text) {
    char out[PinyinComposer::CommitCapacity];
    while (*text)
        Check(p.Feed((uint8_t)*text++, out, sizeof(out)) == PinyinComposer::Composing);
}
static void UnicodeTests() {
    struct Case {
        const char *s;
        uint32_t cp;
        uint8_t n;
    };
    const Case valid[] = {{"A", 0x41, 1},
                          {"\xC2\x80", 0x80, 2},
                          {"\xDF\xBF", 0x7FF, 2},
                          {"\xE0\xA0\x80", 0x800, 3},
                          {"中", 0x4E2D, 3},
                          {"\xED\x9F\xBF", 0xD7FF, 3},
                          {"\xEE\x80\x80", 0xE000, 3},
                          {"\xEF\xBF\xBF", 0xFFFF, 3},
                          {"\xF0\x90\x80\x80", 0x10000, 4},
                          {"\xF4\x8F\xBF\xBF", 0x10FFFF, 4}};
    for (uint32_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        DecodeResult d = Decode(valid[i].s, valid[i].n);
        Check(d.valid && d.bytes == valid[i].n && d.codepoint == valid[i].cp);
        Check(Validate(valid[i].s, valid[i].n + 1));
        Check(!Validate(valid[i].s, valid[i].n));
        for (uint32_t cut = 0; cut < valid[i].n; ++cut)
            Check(!Decode(valid[i].s, cut).valid);
    }
    const char *invalid[] = {"\x80",
                             "\xBF",
                             "\xC0\x80",
                             "\xC1\xBF",
                             "\xC2",
                             "\xC2 ",
                             "\xE0\x80\x80",
                             "\xE0\x9F\xBF",
                             "\xE1\x80",
                             "\xE1 ",
                             "\xED\xA0\x80",
                             "\xED\xBF\xBF",
                             "\xF0\x80\x80\x80",
                             "\xF0\x8F\xBF\xBF",
                             "\xF1\x80\x80",
                             "\xF4\x90\x80\x80",
                             "\xF5\x80\x80\x80",
                             "\xF8\x88\x80\x80\x80",
                             "\xFE",
                             "\xFF"};
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        DecodeResult d = Decode(invalid[i], ByteLength(invalid[i]));
        Check(!d.valid && d.bytes == 1 && d.codepoint == 0xFFFD);
        Check(!Validate(invalid[i]));
    }
    Check(Decode("", 1).valid && Decode("", 1).bytes == 0);
    Check(!Decode(0, 4).valid && !Decode("A", 0).valid);
    Check(!Validate(0) && !Validate("", 0));
    // Enumerate every Unicode scalar, including supplementary planes and boundary values.
    for (uint32_t cp = 1; cp <= 0x10FFFF; ++cp) {
        if (cp >= 0xD800 && cp <= 0xDFFF)
            continue;
        char b[5];
        uint32_t n;
        if (cp < 0x80) {
            b[0] = cp;
            n = 1;
        } else if (cp < 0x800) {
            b[0] = 0xC0 | (cp >> 6);
            b[1] = 0x80 | (cp & 63);
            n = 2;
        } else if (cp < 0x10000) {
            b[0] = 0xE0 | (cp >> 12);
            b[1] = 0x80 | ((cp >> 6) & 63);
            b[2] = 0x80 | (cp & 63);
            n = 3;
        } else {
            b[0] = 0xF0 | (cp >> 18);
            b[1] = 0x80 | ((cp >> 12) & 63);
            b[2] = 0x80 | ((cp >> 6) & 63);
            b[3] = 0x80 | (cp & 63);
            n = 4;
        }
        b[n] = 0;
        DecodeResult d = Decode(b, n);
        Check(d.valid && d.codepoint == cp && d.bytes == n);
    }
    // Physically short arrays let ASan prove Decode stops at available bytes / NUL.
    const char short2[] = {char(0xE1), char(0x80)};
    const char nul2[] = {char(0xF0), 0};
    Check(!Decode(short2, 2).valid);
    Check(!Decode(nul2, 4).valid);
}
static void BufferTests() {
    struct Buffer {
        uint8_t before;
        char data[24];
        uint8_t after;
    } b;
    b.before = 0x5A;
    b.after = 0xA5;
    const char *mixed = "A中文\xF0\x9F\x8C\x8D";
    Check(ByteLength(mixed) == 11 && CodepointCount(mixed) == 4);
    for (uint32_t cap = 0; cap <= sizeof(b.data); ++cap) {
        for (uint32_t i = 0; i < sizeof(b.data); ++i)
            b.data[i] = char(0x7E);
        uint32_t n = Copy(b.data, cap, mixed);
        Check(cap ? n < cap && Validate(b.data, cap) : b.data[0] == char(0x7E));
        for (uint32_t i = cap; i < sizeof(b.data); ++i)
            Check(b.data[i] == char(0x7E));
        Check(b.before == 0x5A && b.after == 0xA5);
    }
    Check(Copy(b.data, sizeof(b.data), mixed, 2) == 4 && Equal(b.data, "A中"));
    Copy(b.data, sizeof(b.data), mixed);
    Check(Copy(b.data, sizeof(b.data), b.data, 2) == 4 && Equal(b.data, "A中"));
    Check(Copy(b.data, sizeof(b.data), "中\xFF文") == 3 && Equal(b.data, "中"));
    Check(Copy(b.data, sizeof(b.data), "中文", 99, 4) == 3 && Equal(b.data, "中"));
    Copy(b.data, sizeof(b.data), mixed);
    Check(Backspace(b.data, sizeof(b.data)) && Equal(b.data, "A中文"));
    Check(Backspace(b.data, sizeof(b.data)) && Equal(b.data, "A中"));
    Check(Backspace(b.data, sizeof(b.data)) && Equal(b.data, "A"));
    Check(Backspace(b.data, sizeof(b.data)) && Equal(b.data, ""));
    Check(!Backspace(b.data, sizeof(b.data)));
    Copy(b.data, sizeof(b.data), "中");
    Check(!Append(b.data, 6, "文") && Equal(b.data, "中"));
    Check(Append(b.data, 7, "文") && Equal(b.data, "中文"));
    Check(!Append(b.data, sizeof(b.data), "\xED\xA0\x80") && Equal(b.data, "中文"));
    Check(Append(b.data, sizeof(b.data), b.data) && Equal(b.data, "中文中文"));
    Check(Append(b.data, sizeof(b.data), b.data + 3) && Equal(b.data, "中文中文文中文"));
    char bad[] = {char(0xFF), 0};
    Check(!Backspace(bad, 2) && (uint8_t)bad[0] == 0xFF);
    char unterminated[3] = {'a', 'b', 'c'};
    Check(!Append(unterminated, 3, "d"));
    Check(!Backspace(unterminated, 3));
    Check(!Append(0, 1, "A") && !Append(b.data, 0, "A") && Copy(0, 2, "A") == 0);
    Check(ContainsAsciiFold("AbC中文DeF", "c中") && ContainsAsciiFold("应用程序", "程序"));
    Check(ContainsAsciiFold("中文", ""));
    Check(!ContainsAsciiFold("中文", "\xB8\xAD"));
    Check(!ContainsAsciiFold("\xFF", "") && !ContainsAsciiFold("你好", "您好"));
}
static void CatalogFontTests() {
    Check(StringCount > 130 && GlyphCount() > 300 && AtlasBytes() < 512 * 1024);
    Check(Equal(Text(SimplifiedChinese, Welcome), "欢迎"));
    Check(Equal(Translate(SimplifiedChinese, "Settings saved"), "设置已保存"));
    Check(Equal(Translate(SimplifiedChinese, "UNREGISTERED LABEL"),
                Text(SimplifiedChinese, UnknownStatus)));
    Check(Equal(Text(SimplifiedChinese, (StringId)0xFFFFFFFFU),
                Text(SimplifiedChinese, UnknownStatus)));
    Check(Equal(LocaleTag(English), "en") && Equal(LocaleTag(SimplifiedChinese), "zh-CN"));
    for (uint32_t i = 0; i < StringCount; ++i) {
        StringId id;
        Check(FindStringId(Text(English, (StringId)i), id));
        Check(CoversText(Text(English, (StringId)i)));
        Check(CoversText(Text(SimplifiedChinese, (StringId)i)));
    }
    Check(!HasGlyph(0x1F30D) && !CoversText("\xF0\x9F\x8C\x8D"));
    Check(!CoversText("\xFF") && !HasGlyph(0) && HasGlyph('A') && HasGlyph(0xFFFD));
    Check(CoversText("中文\nEnglish"));
    for (uint32_t i = 0; i < GlyphCount(); ++i) {
        if (i)
            Check(GlyphCodepoint(i) > GlyphCodepoint(i - 1));
        for (uint32_t scale = 1; scale <= 2; ++scale) {
            Glyph g;
            Check(LookupGlyph(GlyphCodepoint(i), scale, g));
            Check(g.width == 16 * scale && g.height == 18 * scale && g.advance > 0 &&
                  g.advance <= g.width);
            uint32_t nonzero = 0;
            for (uint32_t p = 0; p < g.width * g.height / 2; ++p)
                if (g.pixels[p])
                    ++nonzero;
            Check(nonzero > 0);
        }
    }
    Glyph g = {0, 7, 8, 9};
    Check(!LookupGlyph(0x10FFFF, 1, g) && g.width == 7 && g.height == 8 && g.advance == 9);
    Check(!LookupGlyph(0x4E2D, 0, g) && !LookupGlyph(0x4E2D, 3, g));
    Check(GlyphCodepoint(GlyphCount()) == 0);
}
static void PinyinTests() {
    PinyinComposer p;
    char out[PinyinComposer::CommitCapacity];
    Check(sizeof(p) <= 40 && !p.Active() && p.CandidateCount() == 0 && Equal(p.Candidate(0), ""));
    Check(p.Feed(' ', out, sizeof(out)) == PinyinComposer::PassedThrough);
    Type(p, "ZHONG");
    Check(Equal(p.Preedit(), "zhong") && p.CandidateCount() == 5);
    Check(p.Feed('2', out, sizeof(out)) == PinyinComposer::Committed && Equal(out, "种") &&
          !p.Active());
    Type(p, "nihao");
    Check(p.Feed('9', out, sizeof(out)) == PinyinComposer::Rejected && Equal(p.Preedit(), "nihao"));
    Check(p.Feed(' ', out, 6) == PinyinComposer::Rejected && p.Active() && Equal(out, ""));
    Check(p.Feed(' ', out, 7) == PinyinComposer::Committed && Equal(out, "你好"));
    Type(p, "zhongwen");
    Check(p.Feed('\n', out, sizeof(out)) == PinyinComposer::Committed && Equal(out, "中文"));
    Type(p, "shezhi");
    Check(p.Feed(27, out, sizeof(out)) == PinyinComposer::Canceled && !p.Active() && !out[0]);
    Type(p, "nihao");
    Check(p.Feed('\b', out, sizeof(out)) == PinyinComposer::Composing &&
          Equal(p.Preedit(), "niha"));
    Check(p.Feed(127, out, sizeof(out)) == PinyinComposer::Composing && Equal(p.Preedit(), "nih"));
    p.Cancel();
    Type(p, "a");
    Check(p.Feed('\b', out, sizeof(out)) == PinyinComposer::Composing && !p.Active());
    Type(p, "unknown");
    Check(p.CandidateCount() == 0);
    Check(p.Feed(' ', out, sizeof(out)) == PinyinComposer::Rejected && p.Active());
    Check(p.Feed('0', out, sizeof(out)) == PinyinComposer::Rejected && p.Active());
    Check(p.Feed('.', out, sizeof(out)) == PinyinComposer::Rejected && p.Active());
    Check(p.Feed('\r', out, sizeof(out)) == PinyinComposer::Committed && Equal(out, "unknown"));
    for (uint32_t i = 0; i < PinyinComposer::MaxPreedit; ++i)
        Check(p.Feed('x', out, sizeof(out)) == PinyinComposer::Composing);
    Check(p.Feed('x', out, sizeof(out)) == PinyinComposer::Rejected &&
          ByteLength(p.Preedit()) == PinyinComposer::MaxPreedit);
    Check(p.Feed('\n', out, PinyinComposer::MaxPreedit) == PinyinComposer::Rejected && p.Active());
    Check(p.Feed('\n', out, PinyinComposer::MaxPreedit + 1) == PinyinComposer::Committed &&
          !p.Active());
    Type(p, "ni");
    Check(p.Select(0, 0, 0) == PinyinComposer::Rejected && p.Active());
    Check(p.Select(0, out, 0) == PinyinComposer::Rejected && p.Active());
    p.Cancel();
    Check(DictionaryEntryCount() >= 200);
    for (uint32_t i = 0; i < DictionaryEntryCount(); ++i) {
        Type(p, DictionarySpelling(i));
        Check(p.CandidateCount() == DictionaryCandidateCount(i));
        Check(p.CandidateCount() > 0 && p.CandidateCount() <= 9);
        for (uint32_t c = 0; c < DictionaryCandidateCount(i); ++c) {
            Check(CoversText(DictionaryCandidate(i, c)));
            Check(Equal(p.Candidate(c), DictionaryCandidate(i, c)));
            Check(p.Select(c, out, sizeof(out)) == PinyinComposer::Committed &&
                  Equal(out, DictionaryCandidate(i, c)));
            Type(p, DictionarySpelling(i));
        }
        Check(p.Select(p.CandidateCount(), out, sizeof(out)) == PinyinComposer::Rejected &&
              p.Active());
        p.Cancel();
    }
    Type(p, "shi");
    Check(p.Feed('9', out, sizeof(out)) == PinyinComposer::Committed && Equal(out, "世"));
    Check(!DictionaryCandidateCount(DictionaryEntryCount()));
    Check(Equal(DictionarySpelling(DictionaryEntryCount()), "") &&
          Equal(DictionaryCandidate(0, 99), ""));
}
static int Run() {
    UnicodeTests();
    BufferTests();
    CatalogFontTests();
    PinyinTests();
    return failures ? 1 : 0;
}
#ifdef GTOS_I18N_SANITIZE
extern "C" int puts(const char *);
int main() {
    int rc = Run();
    puts(rc ? "FAIL i18n tests" : "PASS i18n tests");
    return rc;
}
#else
extern "C" void _start() {
    int rc = Run();
    const char *s = rc ? "FAIL i18n tests\n" : "PASS i18n tests\n";
#if defined(__x86_64__)
    long written = 1L;
    asm volatile("syscall" : "+a"(written) : "D"(1L), "S"(s), "d"(16L) : "rcx", "r11", "memory");
    asm volatile("syscall" ::"a"(60L), "D"((long)rc) : "rcx", "r11", "memory");
#else
    int written = 4;
    asm volatile("int $0x80" : "+a"(written) : "b"(1), "c"(s), "d"(16) : "memory");
    asm volatile("int $0x80" ::"a"(1), "b"(rc) : "memory");
#endif
    __builtin_unreachable();
}
#endif
