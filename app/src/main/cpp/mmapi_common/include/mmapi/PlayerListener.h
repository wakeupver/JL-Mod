#ifndef MMAPI_PLAYER_LISTENER_H
#define MMAPI_PLAYER_LISTENER_H

#include <jni.h>
#include <mutex>
#include <atomic>
#include <memory>

namespace mmapi {

    enum PlayerState {
        CLOSED     = 0,
        UNREALIZED = 100,
        REALIZED   = 200,
        PREFETCHED = 300,
        STARTED    = 400,
    };

    enum PlayerListenerEvent {
        RESTART = 1,
        STOP    = 2,
        ERROR   = 3,
    };

    class JNIEnvPtr {
        bool    isJavaThread;
        JNIEnv *env;
    public:
        JNIEnvPtr();
        ~JNIEnvPtr();
        JNIEnv *operator->() const;

        static JavaVM *vm;
    };

    class PlayerListener {
    public:
        PlayerListener(JNIEnv *env, jobject pListener);
        virtual ~PlayerListener();

        void postEvent(PlayerListenerEvent type, int64_t time);

    private:
        struct SharedState {
            std::mutex          guard;
            std::atomic<bool>   closed{false};
            jobject             listener{nullptr};
            jmethodID           method{nullptr};
        };

        std::shared_ptr<SharedState> state;

        void sendEvent(PlayerListenerEvent type, int64_t time);
    };

} // namespace mmapi

#endif
