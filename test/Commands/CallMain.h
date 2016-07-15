extern "C" int printf(const char*,...);

int main(int argc, const char** argv) {
  for (int i = 0; i < argc; ++i) {
    printf("main[%d]: '%s'\n", i, argv[i]);
  }
  return 0;
}
