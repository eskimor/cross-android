#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <jni.h>
#include <stdio.h>
#include <android/log.h>
#include "HsFFI.h"

#include "focus.h"

static native_callbacks* hsCallbacks = NULL;
static JavaVM* jvm = NULL;
static jobject javaCallback = NULL;
static jmethodID evaluateJSCallback = NULL;
static app_callbacks hsAppCallbacks = { NULL };

static int pfd[2];
static pthread_t thr;
static const char *tag = "";

static void *thread_func(void*);

int start_logger(const char *app_name) {
    tag = app_name;

    /* make stdout line-buffered and stderr unbuffered */
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IOLBF, 0);

    /* create the pipe and redirect stdout and stderr */
    pipe(pfd);
    dup2(pfd[1], 1);
    dup2(pfd[1], 2);

    /* spawn the logging thread */
    if(pthread_create(&thr, 0, thread_func, 0) == -1)
        return -1;
    pthread_detach(thr);
    return 0;
}

void *thread_func(void* unused)
{
    ssize_t rdsz;
    char buf[256];
    while((rdsz = read(pfd[0], buf, sizeof buf - 1)) > 0) {
        if(buf[rdsz - 1] == '\n') --rdsz;
        buf[rdsz] = 0;  /* add null-terminator */
        __android_log_write(ANDROID_LOG_DEBUG, tag, buf);
    }
    return 0;
}

JNIEXPORT jint JNICALL JNI_OnLoad ( JavaVM *vm, void *reserved ) {
  jvm = vm;

  start_logger("JSADDLEC");

  static int argc = 5;
  static char *argv[] = {"jsaddle", "+RTS", "-N2", "-I0", "-RTS"};
  static char **pargv = argv;
  hs_init_with_rtsopts(&argc, &pargv);

  hs_add_root (__stginit_Mobile);

  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_systems_obsidian_focus_JSaddleShim_deinit ( JNIEnv *env, jobject thisObj ) {
  hs_exit();
}

JNIEXPORT void JNICALL Java_systems_obsidian_focus_LocalFirebaseInstanceIDService_handleDeviceToken ( JNIEnv *env, jobject thisObj, jstring token ) {
  if(hsAppCallbacks.firebaseInstanceIdService_sendRegistrationToServer) {
    hsAppCallbacks.firebaseInstanceIdService_sendRegistrationToServer(token);
  }
}

JNIEXPORT void JNICALL Java_systems_obsidian_focus_JSaddleShim_processMessage (JNIEnv *env, jobject thisObj, jstring msg) {
  const char *msg_str = (*env)->GetStringUTFChars(env, msg, NULL);
  (*(hsCallbacks->jsaddleResult))(msg_str);
  __android_log_write(ANDROID_LOG_DEBUG, "JSADDLE", "processMessage.jsaddleResult");
  (*env)->ReleaseStringUTFChars(env, msg, msg_str);
  return;
}

JNIEXPORT void JNICALL Java_systems_obsidian_focus_JSaddleShim_injectJavascript (JNIEnv *env) {
  jstring js_str = (*env)->NewStringUTF(env, hsCallbacks->jsaddleJsData);
  (*env)->CallVoidMethod(env, javaCallback, evaluateJSCallback, js_str);
  (*env)->DeleteLocalRef(env, js_str);
  return;
}

JNIEXPORT void JNICALL Java_systems_obsidian_focus_JSaddleShim_init (JNIEnv *env, jobject jsaddleObj) {
  javaCallback = (*env)->NewGlobalRef(env, jsaddleObj);
  jclass cls = (*env)->GetObjectClass(env, javaCallback);
  evaluateJSCallback = (*env)->GetMethodID(env, cls, "evaluateJavascript", "(Ljava/lang/String;)V");
  hsCallbacks = appMain(&evaluateJavascriptWrapper, &hsAppCallbacks);
  return;
}

JNIEXPORT void JNICALL Java_systems_obsidian_focus_JSaddleShim_startProcessing (JNIEnv *env) {
  (*(hsCallbacks->jsaddleStart))();
  return;
}

void evaluateJavascriptWrapper (const char* js) {
  JNIEnv *env;
  jint attachResult = (*jvm)->AttachCurrentThread(jvm, &env, NULL);
  assert (attachResult == JNI_OK);
  jstring js_str = (*env)->NewStringUTF(env, js);
  (*env)->CallVoidMethod(env, javaCallback, evaluateJSCallback, js_str);
  __android_log_write(ANDROID_LOG_DEBUG, "JSADDLEDEBUG", "evaluateJavascriptWrapper.evaluateJSCallback");
  (*env)->DeleteLocalRef(env, js_str);
  jint detachResult = (*jvm) -> DetachCurrentThread(jvm);
  assert (detachResult == JNI_OK);
  return;
}
