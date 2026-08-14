#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <unistd.h>

#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

#include "WiiTrace.h"

namespace
{

// How long a step has to stay unchanged before it is reported, and how often it
// is repeated afterwards.  Long enough that a slow but progressing load stays
// quiet, short enough to notice a stall while watching.
constexpr unsigned int kStallSeconds = 5;

// How often the heap is reported while the game is simply running.  The failure
// this is here to catch takes minutes of play to show up -- streaming in new
// blocks while driving is what grows the heap -- and it is a TREND rather than
// any single number, so it has to be sampled while nothing is going wrong.  Once
// a run has ended, whether the last few lines climb steadily is the difference
// between memory exhaustion and something else entirely.
constexpr unsigned int kHeapReportSeconds = 15;

// The step is written by the game thread and read by the watchdog with no lock.
// A torn read costs one garbled log line and nothing else, which is a better
// trade than putting a mutex on the path every wiiLog() call takes.  The serial
// is what progress is actually judged by, so it is written last.
char s_step[192] = "boot";
volatile unsigned int s_stepSerial;
volatile bool s_watchdogRunning;

lwp_t s_watchdogThread = LWP_THREAD_NULL;
u8 s_watchdogStack[8192] ATTRIBUTE_ALIGN(8);

#if CREATE_LOG

// The game thread appends and the watchdog commits, so the two share a lock.
// newlib's own stdio locking would cover the writes on its own, but not the
// fsync, and holding one lock across both is simpler than reasoning about which
// half is already covered.
FILE *s_logFile;
mutex_t s_logMutex = LWP_MUTEX_NULL;
volatile bool s_logDirty;

// Pushes what has been written out of libfat's cache and onto the card.  fflush
// alone only moves a line out of newlib's buffer into that cache, where a run
// that ends in a freeze leaves it: the console has to be reset, and the cache
// goes with it.  Committing costs far too much to do per line, so the watchdog
// does it once a second, which is also the bound on how much of the tail a
// freeze can take with it.
void
commitLog(void)
{
	if(s_logFile == nullptr || !s_logDirty)
		return;
	LWP_MutexLock(s_logMutex);
	fsync(fileno(s_logFile));
	s_logDirty = false;
	LWP_MutexUnlock(s_logMutex);
}

#endif // CREATE_LOG

void*
watchdogMain(void*)
{
	unsigned int lastSerial = 0;
	unsigned int stalledSeconds = 0;
	unsigned int secondsSinceHeap = 0;

	while(s_watchdogRunning){
		usleep(1000*1000);

		// Before the progress check, so a run that stops making progress still
		// gets everything it had already written onto the card.
#if CREATE_LOG
		commitLog();
#endif

		// Outside the progress check on purpose.  A game running perfectly well
		// is exactly when the heap needs sampling, because a run that ends by
		// vanishing leaves no other evidence of what it was doing beforehand.
		if(++secondsSinceHeap >= kHeapReportSeconds){
			secondsSinceHeap = 0;
			WiiTraceHeap("running");
		}

		if(s_stepSerial != lastSerial){
			lastSerial = s_stepSerial;
			stalledSeconds = 0;
			continue;
		}

		if(++stalledSeconds < kStallSeconds)
			continue;
		stalledSeconds = 0;
		WiiTraceReport("WII watchdog: no progress for %us, last step [%s]\n",
		               kStallSeconds, s_step);
		WiiTraceHeap("watchdog");
	}
	return nullptr;
}

} // namespace

void
WiiTraceOpenLog(const char *directory)
{
#if CREATE_LOG
	if(s_logFile != nullptr || directory == nullptr)
		return;

	char path[192];
	std::snprintf(path, sizeof(path), "%s/debug.log", directory);

	if(LWP_MutexInit(&s_logMutex, false) != 0){
		SYS_Report("WII log: mutex create failed, %s not opened\n", path);
		return;
	}

	// Truncated rather than appended.  This is read after a run that did not
	// finish, and a previous run's tail sitting above this one is the quickest
	// way to misread where the current one stopped.
	s_logFile = std::fopen(path, "w");
	if(s_logFile == nullptr){
		LWP_MutexDestroy(s_logMutex);
		s_logMutex = LWP_MUTEX_NULL;
		SYS_Report("WII log: could not open %s\n", path);
		return;
	}

	WiiTraceReport("WII log: writing to %s\n", path);
#else
	(void)directory;
#endif
}

void
WiiTraceCloseLog(void)
{
#if CREATE_LOG
	if(s_logFile == nullptr)
		return;
	LWP_MutexLock(s_logMutex);
	std::fclose(s_logFile);
	s_logFile = nullptr;
	s_logDirty = false;
	LWP_MutexUnlock(s_logMutex);
	LWP_MutexDestroy(s_logMutex);
	s_logMutex = LWP_MUTEX_NULL;
#endif
}

void
WiiTraceLogLine(const char *message)
{
#if CREATE_LOG
	if(s_logFile == nullptr || message == nullptr)
		return;

	// Trailing newlines are trimmed and one is written back, because the callers
	// disagree: most SYS_Report format strings end in \n and some messages do
	// not, and a log with occasional blank lines and occasional run-on ones is
	// harder to read than either convention on its own.
	size_t length = std::strlen(message);
	while(length > 0 && (message[length - 1] == '\n' || message[length - 1] == '\r'))
		length--;

	// Milliseconds since boot, in front of every line.  For a hang the useful
	// question is not only which line came last but how long it sat there, and a
	// bare step list cannot answer that.
	const unsigned int elapsed = (unsigned int)ticks_to_millisecs(gettime());

	LWP_MutexLock(s_logMutex);
	std::fprintf(s_logFile, "[%8u] %.*s\n", elapsed, (int)length, message);
	// Only out of newlib's buffer; commitLog is what reaches the card.
	std::fflush(s_logFile);
	s_logDirty = true;
	LWP_MutexUnlock(s_logMutex);
#else
	(void)message;
#endif
}

void
WiiTraceReport(const char *format, ...)
{
#if CREATE_LOG
	char message[512];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);

	WiiTraceLogLine(message);
	SYS_Report("%s", message);
#else
	(void)format;
#endif
}

void
WiiTraceNote(const char *message)
{
#if !CREATE_LOG
	// Nothing reads s_step with the watchdog gone, so this would be a strlen and
	// a copy per model streamed, written into a buffer no one looks at.
	(void)message;
#else
	size_t length = std::strlen(message);
	while(length > 0 &&
	      (message[length - 1] == '\n' || message[length - 1] == '\r'))
		length--;
	if(length >= sizeof(s_step))
		length = sizeof(s_step) - 1;
	std::memcpy(s_step, message, length);
	s_step[length] = '\0';
	s_stepSerial++;
#endif
}

void
WiiTraceHeap(const char *tag)
{
#if !CREATE_LOG
	// mallinfo walks every free block to build its answer, so this is not a cheap
	// call to leave in on a console that has already run short of memory.
	(void)tag;
#else
	struct mallinfo info = mallinfo();

	WiiTraceReport("WII heap %s: used=%uK free=%uK blocks=%d top=%uK "
	           "arena1=%uK arena2=%uK\n",
	           tag,
	           (unsigned int)info.uordblks/1024u,
	           (unsigned int)info.fordblks/1024u,
	           info.ordblks,
	           (unsigned int)info.keepcost/1024u,
	           (unsigned int)SYS_GetArena1Size()/1024u,
	           (unsigned int)SYS_GetArena2Size()/1024u);
#endif
}

void
WiiTraceStartWatchdog(void)
{
#if !CREATE_LOG
	// The watchdog exists to report, so with nothing to report to it is a thread
	// and an 8KB stack spent on waking up once a second to decide to stay quiet.
#else
	if(s_watchdogRunning)
		return;
	s_watchdogRunning = true;
	// Above the game thread on purpose.  A genuine hang is a loop that never
	// yields, and a watchdog that only runs when the main thread lets it would
	// stay silent for exactly the case it exists to report.  It sleeps between
	// checks, so the priority costs nothing.
	if(LWP_CreateThread(&s_watchdogThread, watchdogMain, nullptr,
	                    s_watchdogStack, sizeof(s_watchdogStack), 100) != 0){
		s_watchdogRunning = false;
		WiiTraceReport("WII watchdog: thread create failed, loading stalls will "
		               "not be reported, and debug.log will only reach the card "
		               "when the game exits\n");
	}
#endif
}
