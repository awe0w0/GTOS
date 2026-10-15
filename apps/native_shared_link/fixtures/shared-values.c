__attribute__((visibility("default"))) int gtos_shared_counter = 7;

__attribute__((visibility("default"))) int GtosSharedAdd(int value) {
  gtos_shared_counter += value;
  return gtos_shared_counter;
}
