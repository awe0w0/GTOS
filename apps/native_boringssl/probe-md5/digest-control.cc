extern "C" unsigned char *__real_MD5(const unsigned char *, unsigned,
                                     unsigned char *);
extern "C" unsigned char *__wrap_MD5(const unsigned char *input, unsigned length,
                                     unsigned char *output) {
  unsigned char *result = __real_MD5(input, length, output);
  output[0] ^= 1;
  return result;
}
