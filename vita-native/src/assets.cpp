#include "assets.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace rua {
namespace {
uint32_t le32(const uint8_t* p) { return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void read(FILE* file, void* data, size_t size) {
    if (fread(data, 1, size, file) != size) throw std::runtime_error("The original resource pack is incomplete.");
}
}
uint32_t rgba(int r, int g, int b, int a) { return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; }
Assets::~Assets() {
    vita2d_wait_rendering_done();
    for (auto& item : textures_) if (item.second.gpu) vita2d_free_texture(item.second.gpu);
}
void Assets::load(const std::string& path) {
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), fclose);
    if (!file) throw std::runtime_error("Missing data/original.rup. Import the Android assets and package them with the application.");
    std::array<uint8_t, 8> header{};
    read(file.get(), header.data(), header.size());
    uint32_t count = le32(header.data() + 4);
    if (memcmp(header.data(), "RUP1", 4) || count > 512) throw std::runtime_error("Invalid original resource pack.");
    std::vector<std::array<uint8_t, 88>> entries(count);
    read(file.get(), entries.data(), entries.size() * 88);
    for (const auto& entry : entries) {
        if (!memchr(entry.data(), 0, 64)) throw std::runtime_error("Invalid resource name.");
        std::string name(reinterpret_cast<const char*>(entry.data()));
        uint32_t offset = le32(entry.data() + 64), packed = le32(entry.data() + 68), size = le32(entry.data() + 72);
        uint32_t w = le32(entry.data() + 76), h = le32(entry.data() + 80), crc = le32(entry.data() + 84);
        if (size > 16 * 1024 * 1024 || packed > 16 * 1024 * 1024 || w > 2048 || h > 2048 ||
            (w && (!h || size != w * h * 4))) throw std::runtime_error("Invalid resource dimensions: " + name);
        if (fseek(file.get(), offset, SEEK_SET)) throw std::runtime_error("Cannot seek resource pack.");
        std::vector<uint8_t> compressed(packed), bytes(size);
        read(file.get(), compressed.data(), packed);
        uLongf actual = size;
        if (uncompress(bytes.data(), &actual, compressed.data(), packed) != Z_OK || actual != size ||
            crc32(0, bytes.data(), size) != crc) throw std::runtime_error("Damaged original resource: " + name);
        if (!w) { sounds_.emplace(name, std::move(bytes)); continue; }
        auto& texture = textures_[name];
        texture.width = w; texture.height = h;
        texture.gpu = vita2d_create_empty_texture(w, h);
        if (!texture.gpu) throw std::runtime_error("Not enough texture memory: " + name);
        auto* pixels = static_cast<uint8_t*>(vita2d_texture_get_datap(texture.gpu));
        auto stride = vita2d_texture_get_stride(texture.gpu);
        for (uint32_t y = 0; y < h; ++y) memcpy(pixels + size_t(y) * stride, bytes.data() + size_t(y) * w * 4, w * 4);
        vita2d_texture_set_filters(texture.gpu, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
        if (name.find("platform") == 0 || name == "star") {
            texture.alpha.resize(w * h);
            for (size_t i = 0; i < texture.alpha.size(); ++i) texture.alpha[i] = bytes[i * 4 + 3];
        }
    }
}
const Texture& Assets::get(const std::string& name) const {
    auto found = textures_.find(name);
    if (found == textures_.end()) throw std::runtime_error("Missing original texture: " + name);
    return found->second;
}
Renderer::Renderer(const Assets& assets) : assets_(assets), font_(vita2d_load_default_pgf()) {
    if (!font_) throw std::runtime_error("Cannot load Vita system font.");
}
Renderer::~Renderer() {
    if (drawing_) { vita2d_disable_clipping(); vita2d_end_drawing(); }
    vita2d_wait_rendering_done(); vita2d_free_pgf(font_);
}
void Renderer::begin(uint32_t color) {
    vita2d_start_drawing();
    drawing_ = true;
    vita2d_set_clear_color(rgba(12, 3, 25));
    vita2d_clear_screen();
    vita2d_set_clip_rectangle(0, 0, screenWidth, screenHeight);
    vita2d_enable_clipping();
    rect(0, 0, width, height, color);
}
void Renderer::end() { vita2d_disable_clipping(); vita2d_end_drawing(); drawing_ = false; vita2d_swap_buffers(); }
void Renderer::image(const std::string& name, float x, float y, float sx, float sy, uint32_t tint, bool centeredScale) {
    const auto& t = assets_.get(name);
    if (sx != 1 && sy != 1 && centeredScale) {
        const int l = int(-(t.width / 2) * sx), top = int(-(t.height / 2) * sy);
        x += l + t.width / 2; y += top + t.height / 2;
        sx = float(int((t.width / 2) * sx) - l) / t.width;
        sy = float(int((t.height / 2) * sy) - top) / t.height;
    }
    vita2d_draw_texture_tint_scale(t.gpu, x * scaleX, y * scaleY, sx * scaleX, sy * scaleY, tint);
}
void Renderer::sprite(const std::string& name, int frame, int columns, int rows, float x, float y,
                      float factor, float angle, float pivotX, float pivotY, uint32_t tint, bool centeredScale) {
    const auto& t = assets_.get(name);
    int w = t.width / columns, h = t.height / rows;
    frame = std::clamp(frame, 0, columns * rows - 1);
    float u = float((frame % columns) * w) / t.width, v = float((frame / columns) * h) / t.height;
    float du = float(w) / t.width, dv = float(h) / t.height;
    auto* vertices = static_cast<vita2d_texture_vertex*>(vita2d_pool_memalign(4 * sizeof(vita2d_texture_vertex), 8));
    if (!vertices) return;
    float c = std::cos(angle), s = std::sin(angle);
    float x0 = 0, y0 = 0, x1 = float(w), y1 = float(h);
    if (factor != 1 && factor >= 0) {
        if (centeredScale) {
            x0 = float(int(-(w / 2) * factor) + w / 2); x1 = float(int((w / 2) * factor) + w / 2);
            y0 = float(int(-(h / 2) * factor) + h / 2); y1 = float(int((h / 2) * factor) + h / 2);
        } else { x1 = float(int(w * factor)); y1 = float(int(h * factor)); }
    }
    for (int i = 0; i < 4; ++i) {
        float px = (i & 1) ? x1 : x0, py = (i & 2) ? y1 : y0;
        float rx = px - pivotX, ry = py - pivotY;
        vertices[i] = {(x + pivotX + rx * c - ry * s) * scaleX,
                      (y + pivotY + rx * s + ry * c) * scaleY, 0.5f,
                      u + ((i & 1) ? du : 0), v + ((i & 2) ? dv : 0)};
    }
    vita2d_draw_array_textured(t.gpu, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, vertices, 4, tint);
}
void Renderer::number(int value, float centerX, float y, float factor, bool large, bool center) {
    const std::string digits = std::to_string(value);
    const std::string prefix = large ? "num_l_" : "num";
    const int width = assets_.get(prefix + "0").width;
    // Original digit alignment leaves the offset unscaled and skips non-digits.
    const int offset = center ? (width / 2) * int(digits.size()) : 0;
    int advance = 0;
    for (char c : digits) if (c >= '0' && c <= '9') {
        const auto& t = assets_.get(prefix + c);
        image(prefix + c, float(int(centerX) - offset + advance), float(int(y)),
              float(int(t.width * factor)) / t.width, float(int(t.height * factor)) / t.height);
        advance = int(advance + width * factor);
    }
}
void Renderer::text(const std::string& value, float x, float y, float size, uint32_t color, bool center) {
    // Native help/menu text keeps its font proportions within the wider layout.
    float factor = size * scaleY / 18.0f;
    float px = x * scaleX;
    if (center) px -= vita2d_pgf_text_width(font_, factor, value.c_str()) / 2.0f;
    vita2d_pgf_draw_text(font_, int(px), int(y * scaleY), color, factor, value.c_str());
}
void Renderer::rect(float x, float y, float w, float h, uint32_t color) {
    vita2d_draw_rectangle(x * scaleX, y * scaleY, w * scaleX, h * scaleY, color);
}
}
