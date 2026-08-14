#ifndef WIITRACE_H
#define WIITRACE_H

// Loading diagnostics for the Wii port.
//
// The question a plain step log cannot answer is whether a load that shows no
// progress is stuck or merely slow, because the thread that would print the
// answer is the one that stopped.  So the step is recorded by the game thread
// and reported by a separate one.

// The one switch for diagnostics on this port.  At 1 every line goes to
// SYS_Report and into debug.log beside the ELF; at 0 NOTHING below runs at all.
//
// SYS_Report reaches a USB Gecko or a connected debugger and nothing else, so on
// a retail console with neither there is nothing at all to read when the game
// stops responding -- which is the one case these diagnostics exist for.  Wiring
// the same lines to storage is what makes a freeze on real hardware legible.
//
// Turning it off is worth real time rather than just a quieter screen.  The
// expensive part was never SYS_Report, which costs nothing with no cable
// attached: it is the work done to reach it.  wiiLog formats into a 512 byte
// buffer for every model, texture and stream the game touches, and streaming
// touches thousands while driving; WiiTraceNote copies each of those strings
// again; WiiTraceHeap walks the whole heap through mallinfo; and the watchdog is
// a thread with an 8KB stack waking once a second.  At 0 none of that is
// compiled in, and the watchdog is never started.
#define CREATE_LOG 0

// Opens debug.log inside the given directory, and does nothing if one is already
// open, so it can be called again later with a better guess.  Does nothing at
// all unless CREATE_LOG is 1.
void WiiTraceOpenLog(const char *directory);

// Commits the log and closes it.  Only matters for a clean exit: the watchdog
// commits once a second on its own, which is what a run that never reaches an
// exit relies on.
void WiiTraceCloseLog(void);

// One line into the log file and nowhere else.  This is the UNFILTERED sink:
// wiiLog() drops its most frequent lines before printing them, and those are
// exactly the ones naming the file or model a load stopped on.
void WiiTraceLogLine(const char *message);

// One printf-style line into both the log file and SYS_Report.  The Wii port
// calls this everywhere it used to call SYS_Report directly.
void WiiTraceReport(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Records the step the game is in.  Called for every wiiLog() line, including
// the ones too frequent to print, so the watchdog sees fine grained progress
// without the log carrying it.
void WiiTraceNote(const char *message);

// Starts the reporting thread.  It names the current step and reports the heap
// whenever that step has not changed for a while, so a main thread stuck in a
// loop or blocked on storage still gets described.
void WiiTraceStartWatchdog(void);

// One tagged heap line.  Free bytes alone cannot separate the cases that
// matter, so the split is reported too: a large free total spread over many
// blocks with a small top chunk is fragmentation, and allocating less will not
// help, whereas a small free total with a large top chunk is plain exhaustion.
void WiiTraceHeap(const char *tag);

#endif
