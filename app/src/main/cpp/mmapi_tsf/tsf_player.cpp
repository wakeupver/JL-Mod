#define TSF_IMPLEMENTATION
#define TML_IMPLEMENTATION

#include "tsf_player.h"
#include "util/log.h"

#define LOG_TAG    "MMAPI"
#define NUM_CHANNELS 2

namespace mmapi {
namespace tiny {

tsf *Player::soundBank{};

Player::Player(tsf *synth, tml_message *midi, const int64_t duration)
    : BasePlayer(duration), synth(synth), media(midi), currentMsg(midi)
{
    tsf_channel_set_bank_preset(synth, 9, 128, 0);
}

Player::~Player() {
    close();
}

int32_t Player::createPlayer(const char *locator, Player **pPlayer) {
    if (locator == nullptr) return -3;

    tsf *synth = tsf_copy(soundBank);
    if (synth == nullptr) return -1;

    if (strcmp(locator, "device://tone") == 0) {
        *pPlayer = new Player(synth, nullptr, -1);
        return 0;
    }

    tml_message *midi = tml_load_filename(locator);
    if (midi == nullptr) {
        tsf_close(synth);
        return -2;
    }

    unsigned int timeLength = 0;
    tml_get_info(midi, nullptr, nullptr, nullptr, nullptr, &timeLength);
    *pPlayer = new Player(synth, midi, (int64_t)timeLength * 1000LL);
    (*pPlayer)->playTime = 0;
    return 0;
}

oboe::Result Player::createAudioStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           .setPerformanceMode(oboe::PerformanceMode::LowLatency)
           .setSharingMode(oboe::SharingMode::Shared)
           .setFormat(oboe::AudioFormat::Float)
           .setChannelCount(NUM_CHANNELS)
           .setCallback(this)
           .setFormatConversionAllowed(true);

    oboe::Result result = builder.openStream(oboeStream);
    if (result != oboe::Result::OK) {
        oboeStream.reset();
        ALOGE("%s: can't open audio stream. %s", __func__, oboe::convertToText(result));
        return result;
    }

    cachedSampleRate       = oboeStream->getSampleRate();
    synth->outSampleRate   = cachedSampleRate;
    return result;
}

void Player::deallocate() {
    BasePlayer::deallocate();
    seekTime = 0;
}

void Player::close() {
    BasePlayer::close();
    if (media != nullptr) { tml_free(media);  media = nullptr; }
    if (synth != nullptr) { tsf_close(synth); synth = nullptr; }
}

int32_t Player::initSoundBank(const char *sound_bank) {
    tsf *synth = tsf_load_filename(sound_bank);
    if (synth == nullptr) return -1;
    if (Player::soundBank != nullptr) tsf_close(Player::soundBank);
    Player::soundBank = synth;
    return 0;
}

int32_t Player::setDataSource(util::JByteArrayPtr *data) {
    tml_message *midi = tml_load_memory(data->buffer, data->length);
    if (midi == nullptr) return -2;

    unsigned int timeLength = 0;
    tml_get_info(midi, nullptr, nullptr, nullptr, nullptr, &timeLength);
    duration = (int64_t)timeLength * 1000LL;

    if (media != nullptr) tml_free(media);
    media      = midi;
    currentMsg = midi;
    playTime   = 0;
    return 0;
}

oboe::Result Player::prefetch() {
    if (media == nullptr) return oboe::Result::ErrorInvalidState;
    return BasePlayer::prefetch();
}

void Player::processEvents(bool playMode) {
    for (; currentMsg && playTime >= currentMsg->time * 1000LL;
           currentMsg = currentMsg->next)
    {
        switch (currentMsg->type) {
            case TML_PROGRAM_CHANGE:
                tsf_channel_set_presetnumber(synth, currentMsg->channel,
                                             currentMsg->program,
                                             (currentMsg->channel == 9));
                break;
            case TML_NOTE_ON:
                if (playMode)
                    tsf_channel_note_on(synth, currentMsg->channel,
                                        currentMsg->key,
                                        (float)currentMsg->velocity / 127.0f);
                break;
            case TML_NOTE_OFF:
                if (playMode)
                    tsf_channel_note_off(synth, currentMsg->channel, currentMsg->key);
                break;
            case TML_PITCH_BEND:
                tsf_channel_set_pitchwheel(synth, currentMsg->channel,
                                           currentMsg->pitch_bend);
                break;
            case TML_CONTROL_CHANGE:
                tsf_channel_midi_control(synth, currentMsg->channel,
                                         currentMsg->control,
                                         currentMsg->control_value);
                break;
            default:
                break;
        }
    }
}

oboe::DataCallbackResult
Player::onAudioReady(oboe::AudioStream *, void *audioData, int32_t numFrames) {
    memset(audioData, 0, sizeof(float) * NUM_CHANNELS * numFrames);

    if (seekTime == -1 && currentMsg == nullptr) {
        seekTime = 0;
        if (looping == -1 || (--loopCount) > 0) {
            playerListener->postEvent(RESTART, playTime);
        } else {
            state = PREFETCHED;
            playerListener->postEvent(STOP, playTime);
            return oboe::DataCallbackResult::Stop;
        }
    }

    if (seekTime != -1) {
        if (seekTime < playTime) {
            tsf_reset(synth);
            tsf_channel_set_bank_preset(synth, 9, 128, 0);
            currentMsg = media;
        } else {
            tsf_note_off_all(synth);
        }
        playTime = seekTime - 1;
        seekTime = -1;
        processEvents(false);
    }

    const int32_t sr = cachedSampleRate > 0 ? cachedSampleRate : 44100;
    int sampleBlock  = TSF_RENDER_EFFECTSAMPLEBLOCK;
    auto *stream     = static_cast<float *>(audioData);

    for (int remaining = numFrames; remaining > 0; remaining -= sampleBlock) {
        if (sampleBlock > remaining) sampleBlock = remaining;

        playTime += (int64_t)sampleBlock * 1000000LL / sr;
        processEvents(true);

        tsf_render_float(synth, stream, sampleBlock);

        for (int j = 0; j < sampleBlock; ++j) {
            stream[j * 2    ] *= gainLeft;
            stream[j * 2 + 1] *= gainRight;
        }
        stream += sampleBlock * NUM_CHANNELS;
    }
    return oboe::DataCallbackResult::Continue;
}

} // namespace tiny
} // namespace mmapi
