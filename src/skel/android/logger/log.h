//
// Created by mrxenginner on 11/05/2025.
//

#ifndef REVC_LOGGER_H
#define REVC_LOGGER_H

#if defined ANDROID

namespace Logger {
    void Log(const char* fmt, ...);
    void CrashLog(const char* fmt, ...);
}
#endif

#endif
