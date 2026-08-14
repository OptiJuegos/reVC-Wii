#pragma once

#ifdef NINTENDO_WII

extern "C" void wiiLog(const char *format, ...)
	__attribute__((format(printf, 1, 2)));

#endif // NINTENDO_WII
