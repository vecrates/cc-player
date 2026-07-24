#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "Player.h"
#include "platform/log.h"

using namespace ccplayer;

static void jniLogFunc(LogLevel level, const char* tag, const char* msg) {
    int prio;
    switch (level) {
        case LogLevel::Debug: prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::Info:  prio = ANDROID_LOG_INFO;  break;
        case LogLevel::Warn:  prio = ANDROID_LOG_WARN;  break;
        case LogLevel::Error: prio = ANDROID_LOG_ERROR; break;
        default: prio = ANDROID_LOG_VERBOSE;
    }
    __android_log_print(prio, tag, "%s", msg);
}

struct PlayerContext {
    Player* player;
    JavaVM* jvm;
    jobject javaPlayer;
};

static void cbOnPrepared(void* userData) {
    auto* ctx = static_cast<PlayerContext*>(userData);
    JNIEnv* env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jclass cls = env->GetObjectClass(ctx->javaPlayer);
    jmethodID mid = env->GetMethodID(cls, "nativeOnPrepared", "()V");
    env->CallVoidMethod(ctx->javaPlayer, mid);
    env->DeleteLocalRef(cls);
}

static void cbOnCompletion(void* userData) {
    auto* ctx = static_cast<PlayerContext*>(userData);
    JNIEnv* env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jclass cls = env->GetObjectClass(ctx->javaPlayer);
    jmethodID mid = env->GetMethodID(cls, "nativeOnCompletion", "()V");
    env->CallVoidMethod(ctx->javaPlayer, mid);
    env->DeleteLocalRef(cls);
}

static void cbOnError(int errorCode, void* userData) {
    auto* ctx = static_cast<PlayerContext*>(userData);
    JNIEnv* env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jclass cls = env->GetObjectClass(ctx->javaPlayer);
    jmethodID mid = env->GetMethodID(cls, "nativeOnError", "(I)V");
    env->CallVoidMethod(ctx->javaPlayer, mid, errorCode);
    env->DeleteLocalRef(cls);
}

static void cbOnProgress(int64_t currentMs, int64_t durationMs, void* userData) {
    auto* ctx = static_cast<PlayerContext*>(userData);
    JNIEnv* env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jclass cls = env->GetObjectClass(ctx->javaPlayer);
    jmethodID mid = env->GetMethodID(cls, "nativeOnProgress", "(JJ)V");
    env->CallVoidMethod(ctx->javaPlayer, mid, (jlong)currentMs, (jlong)durationMs);
    env->DeleteLocalRef(cls);
}

static void cbOnSeekComplete(void* userData) {
    auto* ctx = static_cast<PlayerContext*>(userData);
    JNIEnv* env;
    ctx->jvm->AttachCurrentThread(&env, nullptr);
    jclass cls = env->GetObjectClass(ctx->javaPlayer);
    jmethodID mid = env->GetMethodID(cls, "nativeOnSeekComplete", "()V");
    env->CallVoidMethod(ctx->javaPlayer, mid);
    env->DeleteLocalRef(cls);
}

static PlayerContext* getContext(jlong handle) {
    return reinterpret_cast<PlayerContext*>(handle);
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_ccplayer_CCPlayer_nativeCreate(JNIEnv* env, jobject thiz) {
    setLogFunction(jniLogFunc);

    auto* ctx = new PlayerContext();
    ctx->player = new Player();
    env->GetJavaVM(&ctx->jvm);
    ctx->javaPlayer = env->NewGlobalRef(thiz);

    PlayerCallbacks callbacks{};
    callbacks.onPrepared = cbOnPrepared;
    callbacks.onCompletion = cbOnCompletion;
    callbacks.onError = cbOnError;
    callbacks.onProgress = cbOnProgress;
    callbacks.onSeekComplete = cbOnSeekComplete;
    ctx->player->setCallbacks(callbacks);
    ctx->player->setUserData(ctx);

    return reinterpret_cast<jlong>(ctx);
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeSetDataSource(JNIEnv* env, jobject thiz, jlong handle, jstring path) {
    const char* p = env->GetStringUTFChars(path, nullptr);
    getContext(handle)->player->setDataSource(p);
    env->ReleaseStringUTFChars(path, p);
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeSetSurface(JNIEnv* env, jobject thiz, jlong handle, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    getContext(handle)->player->setSurface(window);
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeSetSurfaceSize(JNIEnv* env, jobject thiz, jlong handle, jint w, jint h) {
    getContext(handle)->player->setSurfaceSize(w, h);
}
JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativePrepare(JNIEnv* env, jobject thiz, jlong handle) {
    getContext(handle)->player->prepare();
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativePrepareAsync(JNIEnv* env, jobject thiz, jlong handle) {
    getContext(handle)->player->prepareAsync();
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeStart(JNIEnv* env, jobject thiz, jlong handle) {
    getContext(handle)->player->start();
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativePause(JNIEnv* env, jobject thiz, jlong handle) {
    getContext(handle)->player->pause();
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeResume(JNIEnv* env, jobject thiz, jlong handle) {
    getContext(handle)->player->resume();
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeStop(JNIEnv* env, jobject thiz, jlong handle) {
    getContext(handle)->player->stop();
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeSeekTo(JNIEnv* env, jobject thiz, jlong handle, jlong positionMs) {
    getContext(handle)->player->seekTo(positionMs);
}

JNIEXPORT void JNICALL
Java_com_ccplayer_CCPlayer_nativeRelease(JNIEnv* env, jobject thiz, jlong handle) {
    auto* ctx = getContext(handle);
    ctx->player->release();
    delete ctx->player;
    env->DeleteGlobalRef(ctx->javaPlayer);
    delete ctx;
}

JNIEXPORT jlong JNICALL
Java_com_ccplayer_CCPlayer_nativeGetCurrentPosition(JNIEnv* env, jobject thiz, jlong handle) {
    return getContext(handle)->player->getCurrentPosition();
}

JNIEXPORT jlong JNICALL
Java_com_ccplayer_CCPlayer_nativeGetDuration(JNIEnv* env, jobject thiz, jlong handle) {
    return getContext(handle)->player->getDuration();
}

JNIEXPORT jint JNICALL
Java_com_ccplayer_CCPlayer_nativeGetState(JNIEnv* env, jobject thiz, jlong handle) {
    return static_cast<jint>(getContext(handle)->player->getState());
}

} // extern "C"
