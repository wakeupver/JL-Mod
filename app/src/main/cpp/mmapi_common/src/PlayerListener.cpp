#include <thread>
#include "PlayerListener.h"
#include "log.h"

#define LOG_TAG "MMAPI"

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    mmapi::JNIEnvPtr::vm = vm;
    return JNI_VERSION_1_6;
}

namespace mmapi {

JavaVM *JNIEnvPtr::vm = nullptr;

JNIEnvPtr::JNIEnvPtr() : isJavaThread(false), env(nullptr) {
    jint res = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (res == JNI_OK) {
        isJavaThread = true;
        return;
    }
    if (res != JNI_EDETACHED) {
        ALOGE("%s: JavaVM::GetEnv() returned %d", __func__, res);
        return;
    }
    res = vm->AttachCurrentThread(&env, nullptr);
    if (res != JNI_OK) {
        ALOGE("%s: JavaVM::AttachCurrentThread() returned %d", __func__, res);
    }
}

JNIEnvPtr::~JNIEnvPtr() {
    if (isJavaThread) return;
    jint res = vm->DetachCurrentThread();
    if (res != JNI_OK) {
        ALOGE("%s: JavaVM::DetachCurrentThread() returned %d", __func__, res);
    }
}

JNIEnv *JNIEnvPtr::operator->() const {
    return env;
}

PlayerListener::PlayerListener(JNIEnv *env, jobject pListener)
    : state(std::make_shared<SharedState>())
{
    state->listener = env->NewGlobalRef(pListener);
    state->method   = env->GetMethodID(
            env->GetObjectClass(state->listener), "postEvent", "(IJ)V");
}

PlayerListener::~PlayerListener() {
    std::lock_guard<std::mutex> lock(state->guard);
    state->closed.store(true, std::memory_order_release);
    if (state->listener != nullptr) {
        JNIEnvPtr env;
        env->DeleteGlobalRef(state->listener);
        state->listener = nullptr;
    }
}

void PlayerListener::sendEvent(PlayerListenerEvent type, int64_t time) {
    if (state->closed.load(std::memory_order_acquire)) return;

    std::lock_guard<std::mutex> lock(state->guard);

    if (state->closed.load(std::memory_order_relaxed)) return;
    if (state->listener == nullptr || state->method == nullptr) {
        ALOGE("%s: listener=%p, method=%p", __func__, state->listener, state->method);
        return;
    }

    JNIEnvPtr env;
    env->CallVoidMethod(state->listener, state->method,
                        static_cast<jint>(type), static_cast<jlong>(time));
}

void PlayerListener::postEvent(PlayerListenerEvent type, int64_t time) {
    auto s = state;
    std::thread([s, this, type, time]() {
        sendEvent(type, time);
    }).detach();
}

} // namespace mmapi
