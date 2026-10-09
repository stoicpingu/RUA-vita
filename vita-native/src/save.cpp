#include "save.hpp"
#include "original_semantics.hpp"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace rua {
namespace {
constexpr const char* directory = "ux0:data/robotunicorn-native";
constexpr const char* filename = "ux0:data/robotunicorn-native/save.dat";
constexpr const char* backup = "ux0:data/robotunicorn-native/save.bak";
constexpr const char* pending = "ux0:data/robotunicorn-native/save.new";
using Bytes = std::array<uint8_t, 56>;
uint32_t get(const Bytes& b, int i) { return uint32_t(b[i]) | uint32_t(b[i+1]) << 8 | uint32_t(b[i+2]) << 16 | uint32_t(b[i+3]) << 24; }
void put(Bytes& b, int i, uint32_t n) { for (int k = 0; k < 4; ++k) b[i+k] = uint8_t(n >> (k*8)); }
bool readSave(const char* path, Save& save) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    Bytes b{}; bool good = fread(b.data(), 1, b.size(), f) == b.size() && fgetc(f) == EOF;
    fclose(f);
    if (!good || memcmp(b.data(), "RUAS", 4) || get(b, 4) != 1 || crc32(0, b.data(), 52) != get(b, 52)) return false;
    uint32_t flags = get(b, 8);
    save.music = flags & 1; save.effects = flags & 2; save.touchButtons = flags & 4;
    for (int i = 0; i < 5; ++i) save.best[i] = get(b, 12+i*4);
    std::sort(save.best.begin(), save.best.end(), [](uint32_t a, uint32_t c) { return javaInt(a) > javaInt(c); });
    save.fairies = get(b, 32); save.stars = get(b, 36); save.games = get(b, 40);
    save.lifetimeScore = get(b, 44); save.achievements = get(b, 48);
    return true;
}
}
void Save::load() { if (!readSave(filename, *this)) readSave(backup, *this); }
bool Save::store() {
    sceIoMkdir("ux0:data", 0777); sceIoMkdir(directory, 0777);
    Bytes b{}; memcpy(b.data(), "RUAS", 4); put(b, 4, 1);
    put(b, 8, (music ? 1 : 0) | (effects ? 2 : 0) | (touchButtons ? 4 : 0));
    for (int i = 0; i < 5; ++i) put(b, 12+i*4, best[i]);
    put(b, 32, fairies); put(b, 36, stars); put(b, 40, games); put(b, 44, lifetimeScore); put(b, 48, achievements);
    put(b, 52, crc32(0, b.data(), 52));
    FILE* f = fopen(pending, "wb");
    if (!f) return writable = false;
    bool good = fwrite(b.data(), 1, b.size(), f) == b.size();
    good = fflush(f) == 0 && good;
    good = fclose(f) == 0 && good;
    if (!good) return writable = false;
    // Do not rotate a damaged main file over the last readable backup.
    Save previous;
    if (readSave(filename, previous)) {
        std::remove(backup);
        if (std::rename(filename, backup)) return writable = false;
    } else std::remove(filename);
    if (std::rename(pending, filename)) return writable = false;
    return writable = true;
}
int Save::record(int32_t score) {
    for (int i = 0; i < 5; ++i) if (score > javaInt(best[i])) {
        for (int j = 4; j > i; --j) best[j] = best[j-1];
        best[i] = score; return i;
    }
    return -1;
}
}
