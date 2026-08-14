//
// Created by mrxenginner on 14/07/2025.
//

#include "common.h"
#include "JavaWrapper.h"

#if defined ANDROID

extern "C" JavaVM *javaVM;

JNIEnv* CJavaWrapper::GetEnv() {

    JNIEnv* env = NULL;

    int getEnvStat = javaVM->GetEnv((void**)&env, JNI_VERSION_1_6);

    if (getEnvStat == JNI_EDETACHED) {
        debug("GetEnv: not attached");
        if (javaVM->AttachCurrentThread(&env, NULL) != 0) {
            debug("Failed to attach");
            return NULL;
        }
    }

    if (getEnvStat == JNI_EVERSION) {
        debug("GetEnv: version not supported");
        return NULL;
    }

    if (getEnvStat == JNI_ERR) {
        debug("GetEnv: JNI_ERR");
        return NULL;
    }
    return env;
}

void CJavaWrapper::ExitGame() {

    JNIEnv* env = GetEnv();

    if (!env) {
        debug("No env");
        return;
    }

    env->CallVoidMethod(this->activity, this->s_ExitGame);
}

CJavaWrapper::CJavaWrapper(JNIEnv *env, jobject activity) {
    this->activity = env->NewGlobalRef(activity);

    jclass nvEventClass = env->GetObjectClass(activity);

    if (!nvEventClass) {
        debug("nvEventClass null");
        return;
    }

    s_ExitGame = env->GetMethodID(nvEventClass, "exitGame", "()V");
    env->DeleteLocalRef(nvEventClass);
    env->DeleteLocalRef(activity);
}

CJavaWrapper::~CJavaWrapper() {
    JNIEnv* pEnv = GetEnv();
    if (pEnv) {
        pEnv->DeleteGlobalRef(this->activity);
    }
}

CJavaWrapper* g_pJavaWrapper = nullptr;
#endif