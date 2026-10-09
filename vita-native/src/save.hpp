#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace rua {
struct Save {
    bool music = true, effects = true, touchButtons = true;
    std::array<uint32_t, 5> best{};
    uint32_t fairies = 0, stars = 0, games = 0, lifetimeScore = 0, achievements = 0;
    bool writable = true;
    void load();
    bool store();
    int record(int32_t score);
};
}
