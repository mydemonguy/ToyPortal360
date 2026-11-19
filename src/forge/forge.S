/*
 * ToyPortal360 - assembly for the Xenon Forge build.
 *
 * Needs Xenon Forge's src/sdk/thunks/macros.inc on the include path.
 */

#include "macros.inc"

/*
 * XAM's Win32 exports that Xenon Forge's generated stubs do not cover yet
 * (ordinals from Xenia's xam_table.inc). Library 1 is xam.xex.
 */
xbox360_stub ReadFile, 0x0101041c
xbox360_stub WriteFile, 0x0101041e
xbox360_stub SetFilePointer, 0x01010417
xbox360_stub GetFileAttributesA, 0x0101044b
xbox360_stub CreateDirectoryA, 0x01010445
xbox360_stub FindFirstFileA, 0x01010449
xbox360_stub FindNextFileA, 0x0101044a
xbox360_stub CloseHandle, 0x01010414
xbox360_stub Sleep, 0x01010436
xbox360_stub GetTickCount, 0x01010415

/*
 * DWORD tp_ccall(fn, a0..a6): calls the console function `fn` with up to
 * seven word arguments, from a frame that holds the home area (0x10-0x50)
 * console code stores its arguments in.
 */
	.section .text.tp_ccall, "ax", @progbits
	.globl tp_ccall
	.type tp_ccall, @function
	.balign 4
tp_ccall:
	stwu 1, -0x60(1)
	mflr 0
	stw 0, 0x64(1)
	mtctr 3
	mr 3, 4
	mr 4, 5
	mr 5, 6
	mr 6, 7
	mr 7, 8
	mr 8, 9
	mr 9, 10
	bctrl
	lwz 0, 0x64(1)
	mtlr 0
	addi 1, 1, 0x60
	blr
	.size tp_ccall, .-tp_ccall

/*
 * Entries the game calls: its patched imports jump here. Console callers
 * expect r14-r31 back in full 64 bits, which SysV code saves only in part,
 * so each entry saves them, keeps its own link register inside its frame
 * (not in the caller's), and calls the C++ hook with r3-r10 unchanged.
 */
.macro tp_entry name, target
	.section .text.\name, "ax", @progbits
	.globl \name
	.type \name, @function
	.balign 4
\name:
	stwu 1, -0xe0(1)
	mflr 0
	stw 0, 0x8(1)
	.irp reg, 14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	std \reg, 0x50 + 8 * (\reg - 14)(1)
	.endr
	bl \target
	.irp reg, 14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	ld \reg, 0x50 + 8 * (\reg - 14)(1)
	.endr
	lwz 0, 0x8(1)
	mtlr 0
	addi 1, 1, 0xe0
	blr
	.size \name, .-\name
.endm

	tp_entry tp_entry_get_raw, HookGetRaw
	tp_entry tp_entry_set_raw, HookSetRaw
	tp_entry tp_entry_get_raw_ex, HookGetRawEx
	tp_entry tp_entry_set_raw_ex, HookSetRawEx
	tp_entry tp_entry_get_proc, HookGetProcAddress
