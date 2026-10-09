#pragma once
#include <vita2d.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace rua {
struct Texture {
    vita2d_texture* gpu = nullptr;
    int width = 0, height = 0;
    std::vector<uint8_t> alpha;
    int pixel(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && !alpha.empty()
            ? alpha[size_t(y) * width + x] : 0;
    }
};
class Assets {
public:
    ~Assets();
    void load(const std::string& path);
    const Texture& get(const std::string& name) const;
    const std::unordered_map<std::string, std::vector<uint8_t>>& sounds() const { return sounds_; }
private:
    std::unordered_map<std::string, Texture> textures_;
    std::unordered_map<std::string, std::vector<uint8_t>> sounds_;
};
class Renderer {
public:
    explicit Renderer(const Assets& assets);
    ~Renderer();
    void begin(uint32_t color);
    void end();
    void image(const std::string& name, float x, float y, float sx = 1, float sy = 1, uint32_t tint = 0xffffffff, bool centeredScale = false);
    void sprite(const std::string& name, int frame, int columns, int rows, float x, float y,
                float scale = 1, float angle = 0, float pivotX = 0, float pivotY = 0, uint32_t tint = 0xffffffff, bool centeredScale = false);
    void number(int value, float centerX, float y, float scale = 1, bool large = false, bool center = true);
    void text(const std::string& value, float x, float y, float size = 14, uint32_t color = 0xffffffff, bool center = false);
    void rect(float x, float y, float w, float h, uint32_t color);
    const Texture& texture(const std::string& name) const { return assets_.get(name); }
    static constexpr float width = 480, height = 320;
    static constexpr int screenWidth = 960, screenHeight = 544;
    static constexpr float scaleX = screenWidth / width, scaleY = screenHeight / height;
private:
    const Assets& assets_;
    vita2d_pgf* font_;
    bool drawing_ = false;
};
uint32_t rgba(int r, int g, int b, int a = 255);
}
