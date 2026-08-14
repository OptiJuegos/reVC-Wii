//
// Created by mrxenginner on 14/07/2025.
//

#ifndef REVC_CJAVAWRAPPER_H
#define REVC_CJAVAWRAPPER_H

#if defined ANDROID

#include <jni.h>
#include <string>

#define EXCEPTION_CHECK(env) \
	if ((env)->ExceptionCheck()) \
	{ \
		(env)->ExceptionDescribe(); \
		(env)->ExceptionClear(); \
		return; \
	}

class CJavaWrapper
{
    jmethodID s_ExitGame;
public:
    jobject activity;

    CJavaWrapper(JNIEnv* env, jobject activity);
    ~CJavaWrapper();

    void ExitGame();

    // static methods
    static JNIEnv* GetEnv();
};

extern CJavaWrapper* g_pJavaWrapper;
#endif

#endif //REVC_CJAVAWRAPPER_H
