#include "audio.hpp"
#include "assets.hpp"
#include <psp2/audioout.h>
#include <mpg123.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace rua {
namespace {
uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
}
void Audio::initialize(const Assets& assets, const std::string& directory) {
    directory_ = directory;
    for (const auto& item : assets.sounds()) {
        const auto& bytes = item.second;
        if (bytes.size() < 12 || memcmp(bytes.data(), "RIFF", 4) || memcmp(bytes.data() + 8, "WAVE", 4))
            throw std::runtime_error("Invalid original sound: " + item.first);
        Sample sample;
        const uint8_t* pcm = nullptr; size_t pcmBytes = 0;
        int bits = 0, encoding = 0;
        for (size_t p = 12; p + 8 <= bytes.size();) {
            uint32_t length = u32(bytes.data() + p + 4);
            if (length > bytes.size() - p - 8) throw std::runtime_error("Truncated sound: " + item.first);
            const uint8_t* data = bytes.data() + p + 8;
            if (!memcmp(bytes.data() + p, "fmt ", 4) && length >= 16) {
                encoding = u16(data); sample.channels = u16(data + 2); sample.rate = u32(data + 4); bits = u16(data + 14);
            } else if (!memcmp(bytes.data() + p, "data", 4)) { pcm = data; pcmBytes = length; }
            p += 8 + length + (length & 1);
        }
        if (!pcm || encoding != 1 || bits != 16 || sample.channels < 1 || sample.channels > 2 ||
            sample.rate < 8000 || sample.rate > 48000 || pcmBytes % (sample.channels * 2))
            throw std::runtime_error("Unsupported original sound: " + item.first);
        sample.data.resize(pcmBytes / 2);
        for (size_t i = 0; i < sample.data.size(); ++i) sample.data[i] = int16_t(u16(pcm + i * 2));
        names_[item.first] = int(samples_.size());
        samples_.push_back(std::move(sample));
    }
    if (mpg123_init() != MPG123_OK) throw std::runtime_error("Cannot initialize music decoder.");
    decoderInitialized_ = true;
    port_ = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    if (port_ < 0) throw std::runtime_error("Cannot open native Vita audio output.");
    mutex_ = sceKernelCreateMutex("rua audio commands", 0, 0, nullptr);
    if (mutex_ < 0) throw std::runtime_error("Cannot create audio command mutex.");
    thread_ = sceKernelCreateThread("rua native audio", threadEntry, 0x10000100, 64 * 1024, 0, 0, nullptr);
    if (thread_ < 0) throw std::runtime_error("Cannot create native audio thread.");
    running_ = true;
    Audio* self = this;
    if (sceKernelStartThread(thread_, sizeof(self), &self) < 0) {
        running_ = false;
        sceKernelDeleteThread(thread_); thread_ = -1;
        throw std::runtime_error("Cannot start native audio thread.");
    }
}
Audio::~Audio() {
    running_ = false;
    if (thread_ >= 0) { sceKernelWaitThreadEnd(thread_, nullptr, nullptr); sceKernelDeleteThread(thread_); }
    if (mutex_ >= 0) sceKernelDeleteMutex(mutex_);
    if (port_ >= 0) { sceAudioOutOutput(port_, nullptr); sceAudioOutReleasePort(port_); }
    if (decoderInitialized_) mpg123_exit();
}
void Audio::enqueue(Command c) {
    if (mutex_ < 0) return;
    sceKernelLockMutex(mutex_, 1, nullptr);
    // A full queue may drop an old effect, but never a newer control command.
    if (write_ - read_ == queue_.size()) ++read_;
    queue_[write_++ % queue_.size()] = c;
    sceKernelUnlockMutex(mutex_, 1);
}
void Audio::play(const std::string& name) {
    auto found = names_.find(name);
    if (found != names_.end()) enqueue({0, found->second, false});
}
void Audio::stopLatest(const std::string& name) {
    auto found = names_.find(name);
    if (found != names_.end()) enqueue({5, found->second, false});
}
void Audio::stopEffects() { enqueue({1, 0, false}); }
void Audio::music(Track track, bool restart) { enqueue({2, int(track), restart}); }
void Audio::settings(bool music, bool effects) { enqueue({3, music ? 1 : 0, effects}); }
void Audio::pause(bool paused) { enqueue({4, 0, paused}); }
int Audio::threadEntry(SceSize, void* argument) { (*static_cast<Audio**>(argument))->run(); return 0; }
void Audio::run() {
    std::array<Voice, 16> voices{}; // Original SoundPool(16, STREAM_MUSIC, 100).
    alignas(64) std::array<std::array<int16_t, 2048>, 2> outputBuffers{};
    alignas(64) std::array<int16_t, 2048> musicBuffer{};
    unsigned bufferIndex = 0;
    uint64_t serial = 0;
    bool paused = false, musicEnabled = true, effectsEnabled = true;
    Track current = Track::Silence;
    mpg123_handle* decoder = nullptr;
    auto closeMusic = [&] { if (decoder) { mpg123_close(decoder); mpg123_delete(decoder); decoder = nullptr; } };
    while (running_) {
        std::array<Command, 64> commands{}; unsigned count = 0;
        sceKernelLockMutex(mutex_, 1, nullptr);
        while (read_ != write_ && count < commands.size()) commands[count++] = queue_[read_++ % queue_.size()];
        sceKernelUnlockMutex(mutex_, 1);
        for (unsigned i = 0; i < count; ++i) {
            const auto& c = commands[i];
            if (c.type == 0 && effectsEnabled && !paused) {
                auto v = std::find_if(voices.begin(), voices.end(), [](const Voice& x) { return x.sample < 0; });
                if (v == voices.end()) v = std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.age < b.age; });
                *v = {c.value, 0, serial++};
            } else if (c.type == 1) { for (auto& v : voices) v.sample = -1; }
            else if (c.type == 2 && (current != Track(c.value) || c.flag)) {
                closeMusic(); current = Track(c.value);
                if (current != Track::Silence) {
                    int error = 0;
                    decoder = mpg123_new(nullptr, &error);
                    if (decoder) {
                        mpg123_param(decoder, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0);
                        mpg123_param(decoder, MPG123_FORCE_RATE, 48000, 0);
                        mpg123_format_none(decoder);
                        mpg123_format(decoder, 48000, MPG123_STEREO, MPG123_ENC_SIGNED_16);
                        std::string path = directory_ + (current == Track::Opening ? "/opening.mp3" : "/game.mp3");
                        if (mpg123_open(decoder, path.c_str()) != MPG123_OK) closeMusic();
                    }
                }
            } else if (c.type == 3) {
                musicEnabled = c.value != 0; effectsEnabled = c.flag;
                if (!effectsEnabled) for (auto& v : voices) v.sample = -1;
            } else if (c.type == 4) paused = c.flag;
            else if (c.type == 5) {
                Voice* latest = nullptr;
                for (auto& v : voices) if (v.sample == c.value && (!latest || v.age > latest->age)) latest = &v;
                if (latest) latest->sample = -1;
            }
        }
        // The audio port can retain the newly submitted buffer until the next
        // output call. Never mix into a buffer still being consumed by DMA.
        auto& output = outputBuffers[bufferIndex];
        output.fill(0); musicBuffer.fill(0);
        if (!paused) {
            if (decoder) {
                size_t filled = 0;
                for (int attempts = 0; filled < sizeof(musicBuffer) && attempts < 8; ++attempts) {
                    size_t done = 0;
                    int result = mpg123_read(decoder, reinterpret_cast<unsigned char*>(musicBuffer.data()) + filled, sizeof(musicBuffer) - filled, &done);
                    filled += done;
                    if (result == MPG123_DONE) {
                        if (mpg123_seek(decoder, 0, SEEK_SET) < 0) { closeMusic(); break; }
                    } else if (result != MPG123_OK && result != MPG123_NEW_FORMAT) { closeMusic(); break; }
                }
            }
            for (int frame = 0; frame < 1024; ++frame) {
                int mixed[2] = {musicEnabled ? musicBuffer[frame * 2] : 0,
                                musicEnabled ? musicBuffer[frame * 2 + 1] : 0};
                for (auto& voice : voices) {
                    if (voice.sample < 0) continue;
                    const auto& sample = samples_[voice.sample];
                    size_t index = size_t(voice.cursor), frames = sample.data.size() / sample.channels;
                    if (index >= frames) { voice.sample = -1; continue; }
                    float blend = float(voice.cursor - index);
                    for (int channel = 0; channel < 2; ++channel) {
                        int source = sample.channels == 1 ? 0 : channel;
                        int a = sample.data[index * sample.channels + source];
                        int b = sample.data[std::min(index + 1, frames - 1) * sample.channels + source];
                        mixed[channel] += int(a + (b - a) * blend);
                    }
                    voice.cursor += sample.rate / 48000.0;
                }
                output[frame * 2] = int16_t(std::clamp(mixed[0], -32768, 32767));
                output[frame * 2 + 1] = int16_t(std::clamp(mixed[1], -32768, 32767));
            }
        }
        if (sceAudioOutOutput(port_, output.data()) < 0) sceKernelDelayThread(20000);
        bufferIndex ^= 1;
    }
    sceAudioOutOutput(port_, nullptr);
    closeMusic();
}
}
