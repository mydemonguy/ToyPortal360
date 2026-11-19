// PowerPC intrinsics the XDK provides, as inline assembly for clang.
#ifndef TOYPORTAL_FORGE_PPCINTRINSICS_H
#define TOYPORTAL_FORGE_PPCINTRINSICS_H

static inline void __dcbst(int offset, void* base)
{
	__asm__ volatile("dcbst %0, %1" : : "b"(offset), "r"(base) : "memory");
}
static inline void __icbi(int offset, void* base)
{
	__asm__ volatile("icbi %0, %1" : : "b"(offset), "r"(base) : "memory");
}
static inline void __sync(void) { __asm__ volatile("sync" : : : "memory"); }
static inline void __isync(void) { __asm__ volatile("isync" : : : "memory"); }
static inline unsigned long long __mftb(void)
{
	unsigned int hi, lo, hi2;
	do
	{
		__asm__ volatile("mftbu %0" : "=r"(hi));
		__asm__ volatile("mftb %0" : "=r"(lo));
		__asm__ volatile("mftbu %0" : "=r"(hi2));
	} while (hi != hi2);
	return ((unsigned long long)hi << 32) | lo;
}

#endif
