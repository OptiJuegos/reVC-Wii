//
// Created by mrxenginner on 11/05/2025.
//

#ifndef REVC_SIGNALHANDLER_H
#define REVC_SIGNALHANDLER_H

#if defined ANDROID

#include "common.h"

namespace CrashHandler {
    void SetupSignalHandlers();
}
#endif

#endif //REVC_SIGNALHANDLER_H