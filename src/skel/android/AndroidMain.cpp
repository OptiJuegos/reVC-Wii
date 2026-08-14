//
// Created by mrxenginner on 13/07/2025.
//

#if defined ANDROID

#include "AndroidMain.h"
#include "JavaWrapper.h"
#include "logger/log.h"
#include <SDL_hints.h>
#include <SDL_main.h>
#include <SignalHandler.h>
#include <patch/patch.h>

JavaVM* javaVM = NULL;
char* StorageRootBuffer = NULL;
uintptr_t g_libREVC = NULL;

bool AndWrapper::AppInitialized = false;
bool AndWrapper::AppStarted = false;

JAVA_WRAPPER Java_org_libsdl_app_SDLActivity_nativeSetupJNI(JNIEnv* env, jobject initGame)
{
    if(!javaVM) {
        env->GetJavaVM(&javaVM);
    }

    g_pJavaWrapper = new CJavaWrapper(env, initGame);
}

JAVA_WRAPPER Java_com_revc_game_core_REVC_setGamePath(JNIEnv *env, jobject obj, jstring value)
{
    const char* root = env->GetStringUTFChars(value, NULL);
    setenv("STORAGE_ROOT", root, 1);

    StorageRootBuffer = getenv("STORAGE_ROOT");
    debug("Storage Root: %s", StorageRootBuffer);

    env->ReleaseStringUTFChars(value, root);
}

bool AndWrapper::InitLibraries() {
	g_libREVC = Patch::FindLib("libreVC.so");

	if (!g_libREVC) {
		Logger::Log("[ERROR]: Required libraries not found!");
		return false;
	}

	Logger::Log("[INFO]: libreVC base: 0x%X", g_libREVC);
	return true;
}

void AndWrapper::TimeInitialize() {
    struct timeval v0;

    gettimeofday(&v0, NULL);
   // base_time = (double)v0.tv_usec / 1000000.0 + (double)v0.tv_sec;
}

void* AndWrapper::GetJNI() {
    return CJavaWrapper::GetEnv();
}

void* AndWrapper::GetJNIFunc() {
    return CJavaWrapper::GetEnv();
}

void* AndWrapper::GetObj() {
    //return s_event_globalThiz;
}

const char* AndWrapper::GetAppId()
{
    /*JNIEnv* CurrentJNIEnv; // x19
    jobject v1; // x20
    const char *v2; // x21
    jboolean v4[4]; // [xsp+Ch] [xbp-14h] BYREF

    if (!staticAppId[0]) {
        CurrentJNIEnv = CJavaWrapper::GetEnv();

        v1 = _JNIEnv::CallObjectMethod(CurrentJNIEnv, s_event_globalThiz, s_GetAppId);
        v2 = CurrentJNIEnv->functions->GetStringUTFChars(CurrentJNIEnv, (jstring)v1, v4);
        strcpy(staticAppId, v2);
        CurrentJNIEnv->functions->ReleaseStringUTFChars(CurrentJNIEnv, (jstring)v1, v2);
        CurrentJNIEnv->functions->DeleteLocalRef(CurrentJNIEnv, v1);
    }
    return staticAppId;*/
}

const char* AndWrapper::GetDeviceID()
{
    /*JNIEnv *CurrentJNIEnv; // x19
    jobject v1; // x20
    const char *v2; // x21
    jboolean v4[4]; // [xsp+Ch] [xbp-24h] BYREF

    CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    v1 = JNIEnv::CallObjectMethod(CurrentJNIEnv, s_event_globalThiz, s_GetDeviceID);
    v2 = CurrentJNIEnv->functions->GetStringUTFChars(CurrentJNIEnv, (jstring)v1, v4);
    strncpy(staticDeviceID, v2, 0x80uLL);
    CurrentJNIEnv->functions->ReleaseStringUTFChars(CurrentJNIEnv, (jstring)v1, v2);
    CurrentJNIEnv->functions->DeleteLocalRef(CurrentJNIEnv, v1);
    return staticDeviceID;*/
}

int AndWrapper::GetDeviceInfo(int index)
{
    //JNIEnv *CurrentJNIEnv; // x0

    //CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    //return _JNIEnv::CallIntMethod(CurrentJNIEnv, s_event_globalThiz, s_GetDeviceInfo, (unsigned int)index);
}

bool AndWrapper::IsAppInstalled(const char* app)
{
    /*_JNIEnv *CurrentJNIEnv; // x20
    __int64 v3; // x19
    bool v4; // w21

    CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    v3 = (__int64)CurrentJNIEnv->functions->NewStringUTF(CurrentJNIEnv, app);
    v4 = _JNIEnv::CallBooleanMethod(CurrentJNIEnv, s_event_globalThiz, s_IsAppInstalled, v3) != 0;
    CurrentJNIEnv->functions->DeleteLocalRef(CurrentJNIEnv, (jobject)v3);
    return v4;*/
}

void AndWrapper::OpenLink(const char* link)
{
    /*_JNIEnv *CurrentJNIEnv; // x20
    __int64 v3; // x19

    CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    v3 = (__int64)CurrentJNIEnv->functions->NewStringUTF(CurrentJNIEnv, link);
    _JNIEnv::CallVoidMethod(CurrentJNIEnv, s_event_globalThiz, s_OpenLink, v3);
    CurrentJNIEnv->functions->DeleteLocalRef(CurrentJNIEnv, (jobject)v3);*/
}

bool AndWrapper::DeviceIsTV()
{
    //_JNIEnv *CurrentJNIEnv; // x0

    //CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    //return _JNIEnv::CallBooleanMethod(CurrentJNIEnv, s_event_globalThiz, s_IsTV) != 0;
}

int AndWrapper::DeviceLocale()
{
   // _JNIEnv *CurrentJNIEnv; // x0

 //   CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
  //  return _JNIEnv::CallIntMethod(CurrentJNIEnv, s_event_globalThiz, s_GetDeviceLocale);
}

int AndWrapper::DeviceType()
{
    //_JNIEnv *CurrentJNIEnv; // x0

    //CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    //return _JNIEnv::CallIntMethod(CurrentJNIEnv, s_event_globalThiz, s_GetDeviceType);
}

void AndWrapper::SystemInitialize()
{

}
/*
int32_t __fastcall NVEventAppMain(int32_t argc, char **argv)
{
    _QWORD *v4; // x8
    pthread_key_t v5; // w0
    __int64 v6; // x21
    _BOOL4 v7; // w21
    _JNIEnv *CurrentJNIEnv; // x0
    _JNIEnv *v9; // x0
    jint v10; // w0
    __int64 v11; // x22
    __int64 v12; // x22
    _JNIEnv *v13; // x0
    int v14; // w8
    _JNIEnv *v15; // x0
    _JNIEnv *v16; // x0
    double v17; // d8
    bool v18; // w19
    double v19; // d1
    double v20; // d0
    double v21; // d12
    double v22; // d8
    float v23; // s0
    char v24; // w20
    pthread_mutexattr_t *v25; // x19
    pthread_mutexattr_t *v26; // x19
    pthread_mutexattr_t *v27; // x19
    struct timeval v29; // [xsp+0h] [xbp-90h] BYREF
    int data; // [xsp+1Ch] [xbp-74h] BYREF

    OS_ApplicationPreinit();
    AND_KeyboardInitialize();
    memset(lastGamepadAxis, 0, sizeof(lastGamepadAxis));
    gettimeofday(&v29, 0LL);
    base_time = (double)v29.tv_usec / 1000000.0 + (double)v29.tv_sec;
    if ( !ANDThread_Initted )
    {
        pthread_key_create(&ANDThreadStorageKey, ANDThreadData::Destroy);
        v4 = malloc(0x18uLL);
        v5 = ANDThreadStorageKey;
        v4[1] = 0LL;
        v4[2] = 0LL;
        *v4 = 0LL;
        pthread_setspecific(v5, v4);
        ANDThread_Initted = 1;
    }
    v6 = operator new(0x30uLL);
    pthread_mutexattr_init((pthread_mutexattr_t *)(v6 + 40));
    pthread_mutexattr_settype((pthread_mutexattr_t *)(v6 + 40), 1);
    pthread_mutex_init((pthread_mutex_t *)v6, (const pthread_mutexattr_t *)(v6 + 40));
    fileMutex = (OSMutex)v6;
    if ( DoInitGraphics )
        initGraphics();
    v7 = 0;
    if ( IsInitGraphics )
        goto LABEL_8;
    while ( IsAndroidPaused || !v7 )
    {
        while ( 1 )
        {
            ++NVEventAppMain(int,char **)::iter;
            v7 = ProcessEvents(0);
            if ( !IsInitGraphics )
                break;
            LABEL_8:
            if ( !IsAndroidPaused )
                goto LABEL_12;
        }
    }
    LABEL_12:
    pthread_mutex_lock((pthread_mutex_t *)AndroidEGLContext);
    CurrentJNIEnv = NVThreadGetCurrentJNIEnv();
    if ( CurrentJNIEnv && s_event_globalThiz )
    {
        if ( !_JNIEnv::CallBooleanMethod(CurrentJNIEnv, s_event_globalThiz, s_makeCurrent) )
            __android_log_print(3, "NVEvent", "Error: MakeCurrent failed");
    }
    else
    {
        __android_log_print(3, "NVEvent", "Error: No valid JNI env in MakeCurrent");
    }
    AND_WRAPPER::SystemInitialize();
    v9 = NVThreadGetCurrentJNIEnv();
    v10 = _JNIEnv::CallIntMethod(v9, s_event_globalThiz, s_GetDeviceType);
    isLowMemoryDevice = (((unsigned int)v10 >> 1) & 1) == 0 || v10 >> 6 < 250;
    v11 = operator new(0x30uLL);
    pthread_mutexattr_init((pthread_mutexattr_t *)(v11 + 40));
    pthread_mutexattr_settype((pthread_mutexattr_t *)(v11 + 40), 1);
    pthread_mutex_init((pthread_mutex_t *)v11, (const pthread_mutexattr_t *)(v11 + 40));
    billingMutex = (OSMutex)v11;
    v12 = operator new(0x30uLL);
    pthread_mutexattr_init((pthread_mutexattr_t *)(v12 + 40));
    pthread_mutexattr_settype((pthread_mutexattr_t *)(v12 + 40), 1);
    pthread_mutex_init((pthread_mutex_t *)v12, (const pthread_mutexattr_t *)(v12 + 40));
    gameServiceMutex = (OSMutex)v12;
    s_conflictHandler = 0LL;
    if ( OS_ApplicationInitialize(argc, (const char **)argv) )
    {
        data = 0;
        AND_AppInitialized = 1;
        v13 = NVThreadGetCurrentJNIEnv();
        if ( _JNIEnv::CallBooleanMethod(v13, s_event_globalThiz, s_IsWifiAvailable) )
        {
            v14 = 2;
        }
        else
        {
            v15 = NVThreadGetCurrentJNIEnv();
            if ( !_JNIEnv::CallBooleanMethod(v15, s_event_globalThiz, s_IsNetworkAvailable) )
                goto LABEL_23;
            v14 = 1;
        }
        data = v14;
        LABEL_23:
        OS_ApplicationEvent(OSET_NetworkChanged, &data);
        v16 = NVThreadGetCurrentJNIEnv();
        if ( v16 && s_event_globalThiz )
        {
            if ( !_JNIEnv::CallBooleanMethod(v16, s_event_globalThiz, s_unMakeCurrent) )
                __android_log_print(3, "NVEvent", "Error: UnMakeCurrent failed");
        }
        else
        {
            __android_log_print(3, "NVEvent", "Error: No valid JNI env in UnMakeCurrent");
        }
        pthread_mutex_unlock((pthread_mutex_t *)AndroidEGLContext);
        if ( OS_ApplicationStartup(windowSize[0], windowSize[1], argc, (const char **)argv) )
        {
            AND_AppStarted = 1;
            gettimeofday(&v29, 0LL);
            if ( !v7 )
            {
                v17 = (double)v29.tv_usec / 1000000.0 + (double)v29.tv_sec;
                do
                {
                    v18 = ProcessEvents(0);
                    while ( !v18 )
                    {
                        if ( !IsAndroidPaused )
                            break;
                        if ( IsAndroidInMultiplayer )
                            break;
                        v18 = ProcessEvents(0);
                        usleep(0x61A8u);
                    }
                    gettimeofday(&v29, 0LL);
                    if ( v29.tv_usec > 1000000 || v29.tv_usec < 0 )
                        v19 = OS_TimeAccurate(void)::last_current_time
                    - (double)(unsigned int)OS_TimeAccurate(void)::last_current_time
                                                                 + 0.00033;
                    else
                    v19 = (double)v29.tv_usec / 1000000.0;
                    v20 = v19 + (double)v29.tv_sec;
                    OS_TimeAccurate(void)::last_current_time = v20;
                    if ( v20 - OS_TimeAccurate(void)::lastPrint > 5.0 )
                    OS_TimeAccurate(void)::lastPrint = v19 + (double)v29.tv_sec;
                    v21 = v20 - base_time;
                    v22 = v20 - base_time - v17;
                    v23 = v22;
                    v24 = OS_ApplicationTick(v23);
                    AND_GamepadUpdate();
                    AND_FileUpdate(v22);
                    AND_BillingUpdate(0);
                    v17 = v21;
                }
                while ( !v18 && (v24 & 1) != 0 );
            }
            OS_ApplicationEvent(OSET_RequestExit, 0LL);
            pthread_key_delete(ANDThreadStorageKey);
            if ( items )
            {
                free(items);
                items = 0LL;
                numItems = 0;
            }
            v25 = (pthread_mutexattr_t *)billingMutex;
            if ( billingMutex )
            {
                pthread_mutex_destroy((pthread_mutex_t *)billingMutex);
                pthread_mutexattr_destroy(v25 + 5);
                operator delete(v25);
            }
            billingMutex = 0LL;
            AND_ClearAchievementData(1);
            v26 = (pthread_mutexattr_t *)billingMutex;
            if ( billingMutex )
            {
                pthread_mutex_destroy((pthread_mutex_t *)billingMutex);
                pthread_mutexattr_destroy(v26 + 5);
                operator delete(v26);
            }
            v27 = (pthread_mutexattr_t *)fileMutex;
            if ( fileMutex )
            {
                pthread_mutex_destroy((pthread_mutex_t *)fileMutex);
                pthread_mutexattr_destroy(v27 + 5);
                operator delete(v27);
            }
            fileMutex = 0LL;
        }
        else
        {
            OS_ApplicationEvent(OSET_RequestExit, 0LL);
        }
    }
    return 0;
}*/
/*
void __fastcall OS_ApplicationEvent(OSEventType type, void *data)
{
    switch ( type )
    {
        case OSET_RequestExit:
            RsGlobal.quit = TRUE;
            //OS_ThreadWait(mainThread);
            break;

        case OSET_Pause:
            SaveGameForPause(eSaveTypes::eExitSave, 0LL);
            CTimer::StartUserPause();
            if ( !CPad::GetPad(0)->DisablePlayerControls
                 && !gMobileMenu.screenStack.numEntries
                 && !CCutsceneMgr::IntroTextIsActiveHack
                 && !CCutsceneMgr::ms_running
                 && !gMobileMenu.pendingScreen
                 && !CTouchInterface::AnyWidgetsUsingAltBack() )
            {
                if (FindPlayerPed())
                    bPendingPause = 1;
            }
            CAEAudioHardware::PauseOpenAL((CAEAudioHardware *)&AEAudioHardware, 1);
            break;
        case OSET_Resume:
            OS_ThreadUnmakeCurrent();
            CTimer::Update();
            if ( !gMobileMenu.screenStack.numEntries && !gMobileMenu.pendingScreen )
                CTimer::EndUserPause();
            CAEAudioHardware::PauseOpenAL((CAEAudioHardware *)&AEAudioHardware, 0);
            break;
        case OSET_LowMemory:
            //DoLowMemoryCleanup = 1;
            break;
        default:
            return;
    }
}*/

JNI_WRAPPER int InitializeGame() {
    debug("Initialize Game");

    if (!AndWrapper::InitLibraries()) return NULL;

    int argc = 0;
    char* argv[1] = { nullptr };

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
   // SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    CrashHandler::SetupSignalHandlers();

    int result = SDL_main(argc, argv);
    return result;
}

#endif