#include <stddef.h>
#include <stdint.h>

static void RunScanCases() {
  int value = 91, count = 92;
  long double number = 9.0L;
  char next = '?';
  Check(sscanf("123", "%*d") == 0, 100);
  Check(sscanf("", "%d", &value) == -1 && value == 91, 101);
  Check(sscanf("", "x") == -1, 102);
  Check(sscanf("x", "y") == 0, 103);
  Check(sscanf("", "") == 0, 104);
  Check(sscanf("", "%n%d", &count, &value) == 0 && count == 0 && value == 91, 105);
  Check(sscanf("123", "%*d%d", &value) == 0 && value == 91, 106);
  int first = 91, second = 92;
  Check(sscanf("1%2", "%d%%%d", &first, &second) == 2 && first == 1 && second == 2, 107);
  count = 92;
  Check(sscanf(" \t%", "%n%%", &count) == 0 && count == 0, 108);
  Check(sscanf("+", "%1d", &value) == 0 && value == 91, 109);
  char text[6] = {'Q', 'Q', 'Q', 'Q', 'Q', 'Q'};
  Check(sscanf("!", "%3[a-z]", text) == 0 && text[0] == 'Q', 110);
  Check(sscanf("abc!", "%3[a-z]%c", text, &next) == 2 &&
        text[0] == 'a' && text[2] == 'c' && text[3] == 0 && text[4] == 'Q' && next == '!', 111);
  const char high[] = {char(0x80), char(0xFF), '!', 0};
  Check(sscanf(high, "%2[^!]%c", text, &next) == 2 &&
        (unsigned char)text[0] == 0x80 && (unsigned char)text[1] == 0xFF && text[2] == 0 && next == '!', 112);
  Check(sscanf("12x", "%2Lf%n%c", &number, &count, &next) == 2 && number == 12.0L && count == 2 && next == 'x', 113);
  Check(sscanf("1e2", "%1Lf%c", &number, &next) == 2 && number == 1.0L && next == 'e', 114);
  Check(sscanf("0x1.8p+2!", "%Lf%n%c", &number, &count, &next) == 2 && number == 6.0L && count == 8 && next == '!', 115);
  number = 9.0L;
  Check(sscanf("0x", "%Lf", &number) == 0 && number == 9.0L, 116);
  Check(sscanf("1e+", "%Lf", &number) == 0 && number == 9.0L, 117);
  Check(sscanf("infiX", "%Lf", &number) == 0 && number == 9.0L, 118);
  Check(sscanf("nan(foo", "%Lf", &number) == 0 && number == 9.0L, 119);
  Check(sscanf("infX", "%Lf%c", &number, &next) == 2 && __builtin_isinf(number) && next == 'X', 120);
  Check(sscanf("-InFiNiTy!", "%Lf%c", &number, &next) == 2 && __builtin_isinf(number) && number < 0 && next == '!', 121);
  Check(sscanf("nanX", "%Lf%c", &number, &next) == 2 && __builtin_isnan(number) && next == 'X', 122);
  Check(sscanf("NaN(payload_1)!", "%Lf%c", &number, &next) == 2 && __builtin_isnan(number) && next == '!', 123);
  Check(sscanf("infX", "%3Lf%c", &number, &next) == 2 && __builtin_isinf(number) && next == 'X', 124);
  Check(sscanf("nanX", "%3Lf%c", &number, &next) == 2 && __builtin_isnan(number) && next == 'X', 125);
  Check(sscanf("0x1.0000000000000002p0", "%Lf", &number) == 1 && number == 0x1.0000000000000002p0L, 126);
  Check(sscanf("0xf.fffffffffffffffp16380", "%Lf", &number) == 1 && number == 0xf.fffffffffffffffp16380L, 127);
  Check(sscanf("0x1p-16382", "%Lf", &number) == 1 && number == 0x1p-16382L, 128);
  Check(sscanf("0x1p-16445", "%Lf", &number) == 1 && number == 0x1p-16445L, 129);
  Check(sscanf("  -123456", "%Lf", &number) == 1 && number == -123456.0L, 130);
  Check(sscanf("7.25", "%Lf", &number) == 1 && number == 7.25L, 131);
  float small = 0;
  double medium = 0;
  Check(sscanf("1.5 2.25", "%f %lf", &small, &medium) == 2 && small == 1.5f && medium == 2.25, 132);
  signed char byte = 0;
  short word = 0;
  long large = 0;
  long long wide = 0;
  Check(sscanf("-12 -123 -12345 -1234567890123", "%hhd %hd %ld %lld", &byte, &word, &large, &wide) == 4 &&
        byte == -12 && word == -123 && large == -12345 && wide == -1234567890123LL, 133);
  unsigned octal = 0, hex = 0;
  Check(sscanf("077 0x1a", "%o %x", &octal, &hex) == 2 && octal == 63 && hex == 26, 134);
  void* pointer = (void*)1;
  Check(sscanf("(nullptr)!", "%p%c", &pointer, &next) == 2 && pointer == nullptr && next == '!', 135);
  Check(sscanf("(nullptr)!", "%*p%c", &next) == 1 && next == '!', 136);
  static char digits[503];
  digits[0] = '1'; digits[1] = '.';
  for (unsigned i = 2; i < 502; ++i) digits[i] = '0';
  digits[502] = 0;
  Check(sscanf(digits, "%Lf%n", &number, &count) == 1 && number == 1.0L && count == 502, 137);
  Check(sscanf("9007199254740993", "%Lf", &number) == 1 && number == 9007199254740993.0L, 138);
  Check(sscanf("18446744073709551615", "%Lf", &number) == 1 && number == 18446744073709551615.0L, 139);
  Check(sscanf("1.000000000000000000108420217248550443400745280086994171142578125", "%Lf", &number) == 1 &&
        number == 0x1.0000000000000002p0L, 140);
}
