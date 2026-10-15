extern int GtosSharedAdd(int value);

__attribute__((visibility("default"))) int GtosSharedUse(int value) {
  return GtosSharedAdd(value) + 3;
}
