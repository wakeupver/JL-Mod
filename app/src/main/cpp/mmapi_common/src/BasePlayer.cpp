#include "BasePlayer.h"
#include "log.h"

#define LOG_TAG "mmapi"

namespace mmapi {

BasePlayer::BasePlayer(const int64_t duration) : duration(duration) {}

BasePlayer::~BasePlayer() {
    delete playerListener;
}

oboe::Result BasePlayer::prefetch() {
    oboe::Result result = createAudioStream();
    if (result == oboe::Result::OK) {
        state = PREFETCHED;
    }
    return result;
}

oboe::Result BasePlayer::start() {
    if (!oboeStream) {
        ALOGE("%s: oboeStream is null", __func__);
        return oboe::Result::ErrorNull;
    }
    oboe::Result result = oboeStream->start();
    if (result != oboe::Result::OK) {
        ALOGE("%s: can't start audio stream. %s", __func__, oboe::convertToText(result));
        return result;
    }
    state = STARTED;
    return result;
}

oboe::Result BasePlayer::pause() {
    if (!oboeStream) return oboe::Result::OK;
    const oboe::StreamState st = oboeStream->getState();
    if (st < oboe::StreamState::Starting || st > oboe::StreamState::Started) {
        return oboe::Result::OK;
    }
    oboe::Result result = oboeStream->pause();
    if (result != oboe::Result::OK) return result;
    state = PREFETCHED;
    return result;
}

void BasePlayer::deallocate() {
    if (oboeStream) {
        oboeStream->stop();
        oboeStream->close();
        oboeStream.reset();
    }
    loopCount = looping;
    state = REALIZED;
}

void BasePlayer::close() {
    if (state == CLOSED) return;
    if (oboeStream) {
        if (state == STARTED) oboeStream->stop();
        if (state >= PREFETCHED) oboeStream->close();
        oboeStream.reset();
    }
    state = CLOSED;
}

int64_t BasePlayer::getMediaTime() {
    return playTime;
}

int64_t BasePlayer::setMediaTime(int64_t now) {
    if (now < 0)             now = 0;
    else if (now > duration) now = duration;
    seekTime = now;
    return now;
}

void BasePlayer::setRepeat(int32_t count) {
    looping   = count;
    loopCount = count;
}

void BasePlayer::setVolume(float left, float right) {
    gainLeft  = left;
    gainRight = right;
}

bool BasePlayer::realize() {
    state = REALIZED;
    return true;
}

void BasePlayer::setListener(PlayerListener *listener) {
    this->playerListener = listener;
}

void BasePlayer::onErrorAfterClose(oboe::AudioStream *, oboe::Result result) {
    if (result == oboe::Result::ErrorDisconnected) {
        oboe::Result res = createAudioStream();
        if (res != oboe::Result::OK) {
            ALOGE("%s: reconnect error=%s", __func__, oboe::convertToText(res));
            if (playerListener) playerListener->postEvent(ERROR, 0);
        } else if (state == STARTED) {
            oboeStream->requestStart();
        }
    } else {
        ALOGE("%s: %s", __func__, oboe::convertToText(result));
        if (playerListener) playerListener->postEvent(ERROR, 0);
    }
}

} // namespace mmapi
