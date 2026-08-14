//
// Created by mrxenginner on 11/05/2025.
//

#if defined ANDROID

#include "common.h"
#include "log.h"
#include "AndroidMain.h"

extern char* StorageRootBuffer;

void Logger::Log(const char *fmt, ...)
{
    static char buffer[512]{};

    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);
#if defined ANDROID
    __android_log_write(ANDROID_LOG_INFO, "LOG", buffer);
#endif
#ifdef USE_FILE_LOG
    static FILE* flLog = nullptr;

    if(flLog == nullptr && StorageRootBuffer != nullptr) {
	    sprintf(buffer, "%slog.txt", StorageRootBuffer);
	    flLog = fopen(buffer, "ab");
    }

    if(flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);
#endif
}

void Logger::CrashLog(const char* fmt, ...)
{
    static char buffer[512]{};
    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);
#if defined ANDROID
    __android_log_write(ANDROID_LOG_FATAL, "CRASH LOG", buffer);
#endif
#ifdef USE_FILE_LOG
    static FILE* flLog = nullptr;

    if (flLog == nullptr && StorageRootBuffer != nullptr) {
	    sprintf(buffer, "%scrash_log.txt", StorageRootBuffer);
	    flLog = fopen(buffer, "ab");
    }

    if (flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);
#endif
}
#endif