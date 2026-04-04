#include "eas_player.h"
#include "util/log.h"
#include "eas_util.h"
#include "libsonivox/eas_reverb.h"

#define LOG_TAG "MMAPI"
#define NUM_COMBINE_BUFFERS 4

namespace mmapi {
namespace eas {

EAS_DLSLIB_HANDLE Player::soundBank{nullptr};

Player::Player(EAS_DATA_HANDLE easHandle, BaseFile *file, EAS_HANDLE stream, const int64_t duration)
    : BasePlayer(duration), easHandle(easHandle), file(file), media(stream)
{
    EAS_DLSLIB_HANDLE dls = Player::soundBank;
    if (dls == nullptr) {
        EAS_SetParameter(easHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_PRESET, EAS_PARAM_REVERB_CHAMBER);
        EAS_SetParameter(easHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_BYPASS, EAS_FALSE);
    } else {
        EAS_SetGlobalDLSLib(easHandle, dls);
    }

    EAS_RESULT result = EAS_OpenMIDIStream(easHandle, &interactive, stream);
    if (result != EAS_SUCCESS) {
        ALOGE("EAS_OpenMIDIStream return: %s", EAS_GetErrorString(result));
    }
}

Player::Player(EAS_DATA_HANDLE easHandle) : Player(easHandle, nullptr, nullptr, -1) {}

Player::~Player() { close(); }

int32_t Player::createPlayer(const char *locator, Player **pPlayer) {
    if (locator == nullptr) return EAS_ERROR_INVALID_PARAMETER;

    EAS_DATA_HANDLE easHandle;
    EAS_RESULT result = EAS_Init(&easHandle);
    if (result != EAS_SUCCESS) return result;

    EAS_SetHeaderSearchFlag(easHandle, false);

    if (strcmp(locator, "device://tone") == 0 ||
        strcmp(locator, "device://midi") == 0) {
        *pPlayer = new Player(easHandle);
        return EAS_SUCCESS;
    }

    BaseFile *file = new IOFile(locator, "rb");
    EAS_HANDLE stream;
    int64_t duration;
    result = openSource(easHandle, file, &stream, &duration);
    if (result != EAS_SUCCESS) {
        EAS_Shutdown(easHandle);
        delete file;
        return result;
    }

    *pPlayer = new Player(easHandle, file, stream, duration);
    (*pPlayer)->playTime = 0;
    return result;
}

oboe::Result Player::createAudioStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           .setPerformanceMode(oboe::PerformanceMode::LowLatency)
           .setSharingMode(oboe::SharingMode::Shared)
           .setFormat(oboe::AudioFormat::I16)
           .setChannelCount(static_cast<int>(easConfig->numChannels))
           .setSampleRate(static_cast<int>(easConfig->sampleRate))
           .setCallback(this)
           .setFramesPerDataCallback(easConfig->mixBufferSize * NUM_COMBINE_BUFFERS);

    oboe::Result result = builder.openStream(oboeStream);
    if (result != oboe::Result::OK) {
        oboeStream.reset();
        ALOGE("%s: can't open audio stream. %s", __func__, oboe::convertToText(result));
    }
    return result;
}

void Player::deallocate() {
    BasePlayer::deallocate();
    if (file != nullptr) seekTime = 0;
}

void Player::close() {
    BasePlayer::close();
    if (media != nullptr) {
        EAS_CloseFile(easHandle, media);
        delete file;
    }
    if (interactive != nullptr) {
        EAS_CloseMIDIStream(easHandle, interactive);
    }
    EAS_Shutdown(easHandle);
    file        = nullptr;
    media       = nullptr;
    interactive = nullptr;
    easHandle   = nullptr;
}

int32_t Player::initSoundBank(const char *sound_bank) {
    EAS_DATA_HANDLE easHandle;
    EAS_RESULT result = EAS_Init(&easHandle);
    if (result != EAS_SUCCESS) return result;

    EAS_SetHeaderSearchFlag(easHandle, false);
    IOFile file(sound_bank, "rb");
    result = EAS_LoadDLSCollection(easHandle, nullptr, &file.easFile);
    if (result == EAS_SUCCESS) {
        EAS_GetGlobalDLSLib(easHandle, &Player::soundBank);
    }
    EAS_Shutdown(easHandle);
    return result;
}

jint Player::writeMIDI(util::JByteArrayPtr &data) {
    EAS_RESULT result = EAS_WriteMIDIStream(easHandle, interactive,
                                            (EAS_U8 *)data.buffer, data.length);
    if (result != EAS_SUCCESS) {
        ALOGE("EAS_WriteMIDIStream return: %s", EAS_GetErrorString(result));
    }
    return data.length;
}

int32_t Player::setDataSource(BaseFile *pFile) {
    EAS_HANDLE stream = nullptr;
    int32_t result = openSource(easHandle, pFile, &stream, &duration);
    if (result != EAS_SUCCESS) return result;

    if (media != nullptr) {
        EAS_CloseFile(easHandle, media);
        delete file;
    }
    media    = stream;
    file     = pFile;
    playTime = 0;
    return result;
}

int32_t Player::openSource(EAS_DATA_HANDLE easHandle,
                            BaseFile        *pFile,
                            EAS_HANDLE      *outStream,
                            int64_t         *outDuration)
{
    EAS_HANDLE stream;
    EAS_RESULT result = EAS_OpenFile(easHandle, &pFile->easFile, &stream);
    if (result != EAS_SUCCESS)
        result = EAS_MMAPIToneControl(easHandle, &pFile->easFile, &stream);
    if (result != EAS_SUCCESS) return result;

    result = EAS_Prepare(easHandle, stream);
    if (result != EAS_SUCCESS) { EAS_CloseFile(easHandle, stream); return result; }

    EAS_I32 type = EAS_FILE_UNKNOWN;
    result = EAS_GetFileType(easHandle, stream, &type);
    if (result != EAS_SUCCESS) { EAS_CloseFile(easHandle, stream); return result; }

    ALOGV("EAS_checkFileType(): %s file recognized", EAS_GetFileTypeString(type));
    if (type == EAS_FILE_UNKNOWN) {
        EAS_CloseFile(easHandle, stream);
        return EAS_ERROR_FILE_FORMAT;
    }

    EAS_I32 length = -1;
    result = EAS_ParseMetaData(easHandle, stream, &length);
    if (result != EAS_SUCCESS) { EAS_CloseFile(easHandle, stream); return result; }

    *outStream   = stream;
    *outDuration = length > 0 ? (int64_t)length * 1000LL : (int64_t)length;
    return EAS_SUCCESS;
}

oboe::Result Player::prefetch() {
    if (media == nullptr && interactive == nullptr)
        return oboe::Result::ErrorInvalidState;

    oboe::Result result = BasePlayer::prefetch();
    if (result != oboe::Result::OK) return result;

    if (file == nullptr) BasePlayer::start();
    return result;
}

oboe::Result Player::pause() {
    if (file == nullptr) return oboe::Result::OK;
    return BasePlayer::pause();
}

oboe::DataCallbackResult
Player::onAudioReady(oboe::AudioStream *, void *audioData, int32_t numFrames) {
    const int numChannels = static_cast<int>(easConfig->numChannels);
    memset(audioData, 0, sizeof(EAS_PCM) * numChannels * numFrames);

    if (seekTime == -1 && media) {
        EAS_STATE easState = EAS_STATE_PLAY;
        EAS_State(easHandle, media, &easState);
        if (easState == EAS_STATE_STOPPED || easState == EAS_STATE_ERROR) {
            seekTime = 0;
            if (looping == -1 || (--loopCount) > 0) {
                playerListener->postEvent(RESTART, playTime);
            } else {
                state = PREFETCHED;
                playerListener->postEvent(STOP, playTime);
                return oboe::DataCallbackResult::Stop;
            }
        }
    }

    if (seekTime != -1 && media) {
        EAS_I32 ms = static_cast<EAS_I32>(seekTime / 1000LL);
        EAS_RESULT result = EAS_Locate(easHandle, media, ms, EAS_FALSE);
        if (result != EAS_SUCCESS) {
            ALOGE("%s: EAS_Locate() return %s", __func__, EAS_GetErrorString(result));
        }
        seekTime = -1;
    }

    auto *stream = static_cast<EAS_PCM *>(audioData);
    int numFramesOutput = 0;

    for (int i = 0; i < NUM_COMBINE_BUFFERS; i++) {
        EAS_I32 numRendered = 0;
        EAS_RESULT result = EAS_Render(easHandle, stream,
                                        easConfig->mixBufferSize, &numRendered);
        if (result != EAS_SUCCESS) {
            playerListener->postEvent(ERROR, result);
            ALOGE("%s: EAS_Render() returned %s, numFramesOutput=%d",
                  __func__, EAS_GetErrorString(result), numFramesOutput);
            return oboe::DataCallbackResult::Stop;
        }

        for (int j = 0; j < numRendered; ++j) {
            if (numChannels >= 1) stream[j * numChannels    ] = (EAS_PCM)(stream[j * numChannels    ] * gainLeft);
            if (numChannels >= 2) stream[j * numChannels + 1] = (EAS_PCM)(stream[j * numChannels + 1] * gainRight);
        }
        stream          += numRendered * numChannels;
        numFramesOutput += numRendered;
    }

    if (media != nullptr) {
        EAS_I32 pTime = -1;
        EAS_RESULT result = EAS_GetLocation(easHandle, media, &pTime);
        if (result != EAS_SUCCESS) {
            ALOGE("%s: EAS_GetLocation return %s", __func__, EAS_GetErrorString(result));
        }
        playTime = pTime != -1 ? (int64_t)pTime * 1000LL : -1;
    }
    return oboe::DataCallbackResult::Continue;
}

} // namespace eas
} // namespace mmapi
