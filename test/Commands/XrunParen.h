
extern "C" int printf(const char*,...);

typedef void (*return_proc) (int);

void XrunReturned( int arg ) {
 printf("%s(%d)\n", __func__, arg);
}

return_proc XrunParen( int arg ) {
 printf("%s(%d)\n", __func__, arg);
 return XrunReturned;
}