#include "assets.hpp"
#include "audio.hpp"
#include "game.hpp"
#include "save.hpp"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <string>

// Texture allocations live in GPU-mapped memory; heap holds masks and PCM samples.
extern "C" { unsigned int _newlib_heap_size_user = 96 * 1024 * 1024; }

namespace {
std::atomic<bool> powerTransition{false};
int powerCallback(int, int, int flags, void*) {
    if (uint32_t(flags) & (SCE_POWER_CB_SYSTEM_SUSPEND | SCE_POWER_CB_SYSTEM_RESUME |
                          SCE_POWER_CB_APP_SUSPEND | SCE_POWER_CB_APP_RESUME)) powerTransition = true;
    return 0;
}
class Controls {
public:
    Controls() {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
        panel_.minAaX = panel_.minAaY = 0; panel_.maxAaX = 1919; panel_.maxAaY = 1087;
        sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel_);
    }
    rua::Input poll() {
        SceCtrlData data{}; data.lx = data.ly = 128; sceCtrlPeekBufferPositive(0, &data, 1);
        uint32_t held = data.buttons;
        if (data.ly < 64) held |= SCE_CTRL_UP;
        if (data.ly > 192) held |= SCE_CTRL_DOWN;
        if (data.lx < 64) held |= SCE_CTRL_LEFT;
        if (data.lx > 192) held |= SCE_CTRL_RIGHT;
        uint32_t pressed = held & ~previous_;
        rua::Input input;
        input.confirm = pressed & SCE_CTRL_CROSS; input.cancel = pressed & SCE_CTRL_CIRCLE;
        input.pause = pressed & SCE_CTRL_START; input.settings = pressed & SCE_CTRL_SELECT;
        input.scores = pressed & SCE_CTRL_TRIANGLE;
        input.up = pressed & SCE_CTRL_UP; input.down = pressed & SCE_CTRL_DOWN;
        input.left = pressed & SCE_CTRL_LEFT; input.right = pressed & SCE_CTRL_RIGHT;
        const bool jump = held & (SCE_CTRL_CROSS | SCE_CTRL_LTRIGGER);
        const bool dash = held & (SCE_CTRL_SQUARE | SCE_CTRL_CIRCLE | SCE_CTRL_RTRIGGER);
        // A logical event replaces the pending event, as in Android. On a simultaneous
        // hardware sample use release, jump, dash priority (last assignment wins).
        if (!jump && jumpHeld_) input.action = rua::Action::Release;
        if (jump && !jumpHeld_) input.action = rua::Action::Jump;
        if (dash && !dashHeld_) input.action = rua::Action::Dash;
        SceTouchData touch{}; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        std::vector<Finger> fingers;
        for (uint32_t i = 0; i < touch.reportNum && i < sizeof(touch.report) / sizeof(touch.report[0]); ++i) {
            const auto& report = touch.report[i];
            // Map the entire panel (including its last sample) into the logical view.
            const float x = (report.x - panel_.minAaX) * rua::Renderer::width /
                std::max(1, int(panel_.maxAaX - panel_.minAaX) + 1);
            const float y = (report.y - panel_.minAaY) * rua::Renderer::height /
                std::max(1, int(panel_.maxAaY - panel_.minAaY) + 1);
            if (x < 0 || x >= rua::Renderer::width || y < 0 || y >= rua::Renderer::height) continue;
            fingers.push_back({report.id, x, y});
        }
        // Keep Android pointer indices stable until a finger leaves.
        std::stable_sort(fingers.begin(), fingers.end(), [this](const Finger& a, const Finger& b) {
            auto index = [this](int id) {
                for (size_t i = 0; i < previousFingers_.size(); ++i) if (previousFingers_[i].id == id) return i;
                return previousFingers_.size();
            };
            return index(a.id) < index(b.id);
        });
        if (fingers.empty() && !previousFingers_.empty()) {
            input.touches.push_back({1, previousFingers_[0].x, previousFingers_[0].y, 0});
        } else if (!fingers.empty()) {
            const auto& first = fingers.front();
            if (previousFingers_.empty()) {
                input.touches.push_back({0, first.x, first.y, 0});
                input.touchPressed = true; input.touchX = first.x; input.touchY = first.y;
            }
            for (size_t i = 0; i < fingers.size(); ++i) {
                bool known = false;
                for (const auto& old : previousFingers_) if (old.id == fingers[i].id) known = true;
                if (!known && (i > 0 || !previousFingers_.empty()))
                    input.touches.push_back({5 | (int(i) << 8), first.x, first.y, fingers.size() > 1 ? fingers[1].x : first.x});
            }
            for (size_t i = 0; i < previousFingers_.size(); ++i) {
                bool remains = false;
                for (const auto& f : fingers) if (f.id == previousFingers_[i].id) remains = true;
                if (!remains) input.touches.push_back({6 | (int(i) << 8), first.x, first.y, 0});
            }
            bool moved = false;
            for (const auto& f : fingers) for (const auto& old : previousFingers_)
                if (f.id == old.id && (f.x != old.x || f.y != old.y)) moved = true;
            if (moved) input.touches.push_back({2, first.x, first.y, fingers.size() > 1 ? fingers[1].x : first.x});
        }
        previous_ = held; previousFingers_ = std::move(fingers); jumpHeld_ = jump; dashHeld_ = dash;
        return input;
    }
private:
    uint32_t previous_ = 0;
    struct Finger { int id; float x, y; };
    std::vector<Finger> previousFingers_;
    bool jumpHeld_ = false, dashHeld_ = false;
    SceTouchPanelInfo panel_{};
};
void merge(rua::Input& a, const rua::Input& b) {
    if (b.action != rua::Action::None) a.events.push_back({b.action, {}, false});
    for (const auto& touch : b.touches) a.events.push_back({rua::Action::None, touch, true});
    a.confirm |= b.confirm; a.cancel |= b.cancel; a.pause |= b.pause; a.settings |= b.settings;
    a.up |= b.up; a.down |= b.down; a.left |= b.left; a.right |= b.right; a.scores |= b.scores;
    if (b.touchPressed) { a.touchPressed = true; a.touchX = b.touchX; a.touchY = b.touchY; }
}
void status(const std::string& message, bool error) {
    vita2d_pgf* font = vita2d_load_default_pgf();
    uint32_t previous = 0;
    do {
        vita2d_start_drawing(); vita2d_set_clear_color(RGBA8(22, 5, 43, 255)); vita2d_clear_screen();
        if (font) {
            vita2d_pgf_draw_text(font, 62, 110, 0xffffffff, 1.4f, "Robot Unicorn Attack");
            size_t position = 0; int line = 0;
            while (position < message.size()) {
                size_t end = std::min(position + 70, message.size());
                if (end < message.size()) {
                    size_t space = message.rfind(' ', end);
                    if (space != std::string::npos && space > position) end = space;
                }
                std::string part = message.substr(position, end - position);
                vita2d_pgf_draw_text(font, 62, 180 + line++ * 28, 0xffffffff, 1, part.c_str());
                position = end + (end < message.size() && message[end] == ' ' ? 1 : 0);
            }
            if (error) vita2d_pgf_draw_text(font, 62, 470, 0xffffffff, 1, "Press O to close.");
        }
        vita2d_end_drawing(); vita2d_swap_buffers();
        if (!error) break;
        SceCtrlData pad{}; sceCtrlPeekBufferPositive(0, &pad, 1);
        if ((pad.buttons & ~previous) & SCE_CTRL_CIRCLE) break;
        previous = pad.buttons;
    } while (true);
    vita2d_wait_rendering_done();
    if (font) vita2d_free_pgf(font);
}
}
int main() {
    if (vita2d_init_advanced(2 * 1024 * 1024) < 0) { sceKernelExitProcess(1); return 1; }
    vita2d_set_vblank_wait(1);
    // Many small original sprites would each consume a 256 KiB CDRAM block.
    // Uncached, GPU-mapped main memory uses 4 KiB granularity and needs no CPU flush.
    vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE);
    scePowerSetArmClockFrequency(444);
    scePowerSetGpuClockFrequency(166);
    SceUID callback = sceKernelCreateCallback("rua power", 0, powerCallback, nullptr);
    if (callback >= 0) scePowerRegisterCallback(callback);
    {
        Controls controls;
        try {
            status("Loading the original dream...", false);
            rua::Assets assets;
            assets.load("app0:data/original.rup");
            rua::Renderer renderer(assets);
            rua::Save save; save.load();
            rua::Audio audio; audio.initialize(assets, "app0:data");
            rua::Game game(assets, renderer, audio, save, sceKernelGetProcessTimeWide());
            uint64_t previous = sceKernelGetProcessTimeWide();
            double accumulated = 0;
            rua::Input pending;
            while (!game.exiting()) {
                sceKernelCheckCallback();
                uint64_t now = sceKernelGetProcessTimeWide();
                double elapsed = (now - previous) / 1000000.0;
                previous = now;
                auto input = controls.poll();
                if (powerTransition.exchange(false)) {
                    game.suspend(); pending = {}; accumulated = 0; input = {};
                }
                game.advanceWall(elapsed);
                merge(pending, input);
                // Same bounded catch-up as Android: at most three 30 Hz steps.
                accumulated = std::min(0.1, accumulated + elapsed);
                while (accumulated >= 1.0 / 30.0) {
                    game.tick(pending); pending = {}; accumulated -= 1.0 / 30.0;
                }
                game.draw();
            }
            save.store();
        } catch (const std::exception& exception) {
            status(exception.what(), true);
        }
    }
    if (callback >= 0) { scePowerUnregisterCallback(callback); sceKernelDeleteCallback(callback); }
    vita2d_fini(); sceKernelExitProcess(0); return 0;
}
