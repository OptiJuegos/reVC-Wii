//
// Created by mrxenginner on 13/07/2025.
//

#ifndef REVC_ANDROIDMAIN_H
#define REVC_ANDROIDMAIN_H

#if defined ANDROID

#include "common.h"

#define JNI_WRAPPER extern "C" __attribute__ ((visibility("default")))
#define JAVA_WRAPPER extern "C" JNIEXPORT void JNICALL

extern uintptr_t g_libREVC;
extern char* StorageRootBuffer;

namespace AndWrapper {
    extern bool AppInitialized;
    extern bool AppStarted;

    bool InitLibraries();
    void SystemInitialize();
    void TimeInitialize();
    void* GetJNI();
    void* GetJNIFunc();
    void* GetObj();
    const char* GetAppId();
    const char* GetDeviceID();
    int GetDeviceInfo(int index);
    bool IsAppInstalled(const char* app);
    void OpenLink(const char* link);
    bool DeviceIsTV();
    int DeviceLocale();
    int DeviceType();
}
#endif

#endif //REVC_ANDROIDMAIN_H
