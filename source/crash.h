// Crash screen (DS only): replaces the silent hang of a CPU exception
// (data abort, undefined instruction) with a report on the bottom screen.

#ifndef DELVE_CRASH_H
#define DELVE_CRASH_H

// Installs the handler. Call as early as possible in main().
void crash_install(void);

// Debug builds: raises an undefined-instruction exception to test the
// handler.
void crash_test(void) __attribute__((noreturn));

#endif // DELVE_CRASH_H
