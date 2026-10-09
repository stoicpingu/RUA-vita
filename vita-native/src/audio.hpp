#pragma once
#include <psp2/kernel/threadmgr.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rua {
class Assets;
class Audio {
public:
    enum class Track { Silence, Opening, Game };
    Audio() = default;
    ~Audio();
    void initialize(const Assets& assets, const std::string& directory);
    void play(const std::string& name);
    void stopLatest(const std::string& name);
    void stopEffects();
    void music(Track track, bool restart = false);
    void settings(bool music, bool effects);
    void pause(bool paused);
private:
    struct Sample { std::vector<int16_t> data; int rate = 0, channels = 0; };
    struct Voice { int sample = -1; double cursor = 0; uint64_t age = 0; };
    struct Command { int type = 0, value = 0; bool flag = false; };
    static int threadEntry(SceSize size, void* argument);
    void run();
    void enqueue(Command command);
    std::vector<Sample> samples_;
    std::unordered_map<std::string, int> names_;
    std::array<Command, 64> queue_{};
    unsigned read_ = 0, write_ = 0;
    std::string directory_;
    SceUID thread_ = -1, mutex_ = -1;
    std::atomic<bool> running_{false};
    int port_ = -1;
    bool decoderInitialized_ = false;
};
}
