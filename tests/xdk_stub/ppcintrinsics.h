// Syntax-check stand-in for the XDK's PowerPC intrinsics.
#ifndef XDK_STUB_PPC_H
#define XDK_STUB_PPC_H
void __dcbst(int, void*);
void __icbi(int, void*);
void __sync();
void __isync();
unsigned long long __mftb();
#endif
