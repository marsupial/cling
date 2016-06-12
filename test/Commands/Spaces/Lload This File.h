extern "C" int printf(const char*,...);

void LOADED_FUNC() {
 printf("%s\n", __func__);
}

void LloadThisFile() {
 printf("%s\n", __func__);
}
