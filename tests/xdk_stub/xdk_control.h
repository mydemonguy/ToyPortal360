// Test-side controls for the fake Xbox runtime in xdk_runtime.cpp.
#ifndef XDK_STUB_CONTROL_H
#define XDK_STUB_CONTROL_H
#include "xtl.h"
#include <string>
#include <vector>

namespace fake
{
	// Files: "TPHDD:\x\y" lives at <root>/TPHDD/x/y.
	void set_root(const char* dir);
	std::string host_path(const char* xbox_path);

	// Controller 0 buttons seen by XInputGetState.
	extern volatile WORD buttons;

	// Scripted system UI. Message boxes pop an int (button index, -1 = B),
	// keyboards pop a string ("\x1b" = cancel). An empty script answers B.
	void script_box(int answer);
	void script_keyboard(const char* text);
	int script_left();
	struct Shown { std::string title, text; };
	std::vector<Shown> shown();          // every message box shown so far
	std::vector<std::string> notifications();

	// XAM functions the plugin resolves (the "real" portal functions).
	extern volatile int real_get_calls, real_set_calls;

	// Module/title the plugin sees as running.
	void set_title(void* ldr_entry, DWORD title_id);

	int threads_running();
	extern volatile int system_threads, title_threads; // by ExCreateThread flags
}

#endif
