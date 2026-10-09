#include "game.hpp"
#include <algorithm>
#include <cmath>

namespace rua {
void Game::centered(const std::string& name, float y, float scale) {
    renderer_.image(name, (480 - renderer_.texture(name).width * scale) / 2, y, scale, scale);
}
void Game::drawPlayer() {
    const auto& p = player_;
    const int poseIndex = p.fallLoop ? 7 : p.motion == Motion::DoubleJump ? 1 : p.motion == Motion::Dead ? 6 : int(p.motion);
    float x = p.poses[poseIndex].x + cameraX_, y = p.poses[poseIndex].y + cameraY_;
    std::string name = "run"; int columns = 9, rows = 4;
    float pivotX = 44, pivotY = float((frameHeight() - 10) / 2);
    switch (p.motion) {
        case Motion::Jump: case Motion::DoubleJump: name = "jump"; rows = 1; break;
        case Motion::Fall: name = p.fallLoop ? "fall_loop" : "fall"; columns = p.fallLoop ? 10 : 12; rows = 1; break;
        case Motion::Land: name = "land"; columns = rows = 1; break;
        case Motion::Dash:
            name = "charge"; columns = rows = 4;
            pivotY = 40; break;
        case Motion::Head: case Motion::Dead: {
            float scale = p.headScale;
            renderer_.image("separate_head", x, y, int(247 * scale) / 247.0f, int(111 * scale) / 111.0f);
            return;
        }
        default: break;
    }
    renderer_.sprite(name, p.animation.frame, columns, rows, x, y, 1, p.angle * 0.01745329252f, pivotX, pivotY);
}
void Game::drawWorld() {
    const float cy = cameraY_;
    auto drawEntity = [&](const Entity& e) {
        if (e.alive) renderer_.sprite(e.star ? "star" : "fairy", e.animation.frame, e.star ? 1 : 4, 1,
            e.x + cameraX_, e.y + cy, 1, e.struck ? 0.1745329252f : 0, e.star ? 41 : 0, e.star ? 38 : 0);
    };
    // Preserve creation order within the original draw layers.
    for (int i = -1; i < 3; ++i) {
        const float x = i * 1024.0f - clouds_;
        if (x + 512 >= 0 && x < 480) renderer_.image((i & 1) ? "clouds_1h" : "clouds_0h", x + cameraX_, -75 + cy, 2, 2);
    }
    for (const auto& tile : tiles_) renderer_.image("platform" + std::to_string(tile.type + 1), tile.x + cameraX_, tile.y + cy);
    for (const auto& e : entities_) if (e.order < 2) drawEntity(e);
    static const char* trails[] = {"rainbow_jump", "rainbow_fall", "rainbow_fall_fader", "tail_double_jump"};
    for (const char* name : trails) for (const auto& e : effects_) if (e.visible && e.image == name)
        renderer_.sprite(e.image, e.animation.frame, e.columns, e.rows, e.x + cameraX_, e.y + cy);
    if (player_.motion != Motion::Head && player_.motion != Motion::Dead) drawPlayer();
    // The initial entry fade has an automatic layer below newly created entities and HUD.
    if (entryFadeVisible_) renderer_.sprite("fad_anim", entryFade_.frame, 4, 1, 0, 0, 4);
    size_t ei = 0, di = 0;
    while (ei < entities_.size() || di < dolphins_.size()) {
        if (di == dolphins_.size() || (ei < entities_.size() && entities_[ei].order < dolphins_[di].order)) {
            const auto& e = entities_[ei++];
            if (e.order >= 2) drawEntity(e);
        } else {
            const auto& d = dolphins_[di++];
            if (d.delay <= 0) renderer_.sprite("dolphin", 0, 1, 1, d.x + cameraX_, d.y + cy, 1, d.angle * 0.01745329252f, 36, 18);
        }
    }
    renderer_.image("life" + std::to_string(2 - wish_), 0, 0);
    renderer_.image("score_gradient", 240 - renderer_.texture("score_gradient").width / 2, -4);
    renderer_.number(player_.score, 240, 4);
    for (const char* name : {"star_explodes", "sparkle_effect", "explode"})
        for (const auto& e : effects_) if (e.visible && e.image == name)
            renderer_.sprite(e.image, e.animation.frame, e.columns, e.rows, e.x + cameraX_, e.y + cy);
    for (const auto& popup : popups_) if (popup.alive) {
        const auto& gradient = renderer_.texture("score_gradient");
        renderer_.image("score_gradient", popup.x - (popup.fairy ? gradient.width / 2.5f : float(gradient.width / 3)),
                        popup.y - gradient.height / 4, popup.fairy ? 0.5f : 0.8f, popup.fairy ? 0.5f : 0.8f, 0xffffffff, true);
    }
    auto notice = [&](const Notice& n, const std::string& name) {
        const float x = n.x, y = n.y;
        if (n.textVisible) renderer_.image(name, x, y, n.scale, n.scale, 0xffffffff, true);
        if (n.sparkleVisible) renderer_.sprite("sparkle_effect", n.sparkle.frame, 3, 2, (480 - 105) / 2, (320 - 107) / 2);
        if (n.circleVisible) renderer_.sprite("white_circle_fade", n.circle.frame, 4, 1, 196, 116);
    };
    notice(dashNotice_, "dash_now");
    if (player_.motion == Motion::Head || player_.motion == Motion::Dead) drawPlayer();
    notice(wishNotice_, "wish" + std::to_string(3 - wish_));
    for (const auto& popup : popups_) if (popup.alive && popup.finishing) {
        const float size = popup.fairy ? 0.5f : 1;
        const int digitWidth = renderer_.texture("num0").width;
        const int digitHeight = renderer_.texture("num0").height;
        const int count = int(std::to_string(popup.score).size());
        renderer_.sprite("sparkle_effect", popup.sparkle.frame, 3, 2,
            popup.x - (95 + 10) / 2 + count * digitWidth / 2,
            popup.y - (97 + 10) / 2 + digitHeight / 2, size, 0, 0, 0, 0xffffffff, true);
        renderer_.sprite("white_circle_fade", popup.circle.frame, 4, 1,
            popup.x - 88 / 2 + count * digitWidth / 2, popup.y - 88 / 2 + digitHeight / 2, size, 0, 0, 0, 0xffffffff, true);
    }
    renderer_.image("btn_info", 445, 5);
    if (save_.touchButtons) {
        const auto& jump = renderer_.texture("btn_jump_up");
        const auto& attack = renderer_.texture("btn_attack_up");
        renderer_.image("btn_jump_up", 16, 320 - jump.height - 14);
        renderer_.image("btn_attack_up", 480 - attack.width - 16, 320 - attack.height - 14);
    }
    for (const auto& popup : popups_) if (popup.alive)
        renderer_.number(popup.score, popup.x + (popup.fairy ? 5 : -5), popup.y + (popup.fairy ? 5 : 0),
                         popup.fairy ? 0.5f : 1, false, false);
}
void Game::drawResults(bool final) {
    renderer_.image("endgame_scores", 0, 0);
    renderer_.image("txt_end_game_header" + std::to_string(deathCause_ + 1), 0, 0);
    static const char* ordinal[] = {"1st", "2nd", "3rd"};
    for (int i = 0; i < 3; ++i) {
        float y = 37 + i * 30;
        std::string name = std::string("score_") + ordinal[i] + "_wish";
        renderer_.image(name + "_bg", 0, y);
        if (i == wish_ && !final) renderer_.sprite("wish_pulse", resultPulse_.frame, 1, 6, 0, y + 3);
        renderer_.image(name, 0, y);
        if (i <= wish_) renderer_.number(scores_[i], 256, y + 3);
    }
    renderer_.sprite("tear", resultTear_.frame, 10, 4, 225, 213);
    if (promptSparkleVisible_) renderer_.sprite("sparkle_effect", resultPromptSparkle_.frame, 3, 2, 60, 238);
    renderer_.sprite("sparkle_over_text", resultSparkle_.frame, 3, 5, 50, 278, 0.44f);
    const float promptY = 276 - (promptBounceTick_ < 10 && (promptBounceTick_ & 1) ? 1 : 0);
    if (final) {
        renderer_.image("finalscore", 0, 127);
        int total = javaAdd(javaAdd(scores_[0], scores_[1]), scores_[2]);
        renderer_.number(total, 256, 132, 1.0f, true);
        if (bestRank_ >= 0) {
            renderer_.sprite("sparkle_over_text", int(uiTime_ * 20) % 15, 3, 5, 132, 153, 0.8f);
            renderer_.text(bestRank_ == 0 ? "NEW HIGH SCORE" : "TOP FIVE SCORE", 240, 185, 13, rgba(255, 246, 143), true);
        }
        renderer_.image("txt_end_game", 0, promptY);
        renderer_.image("txt_end_game_reflection", 0, 276);
        renderer_.text("X: play again   Triangle: high scores   O: title", 240, 266, 10, 0xffffffff, true);
    } else {
        renderer_.image("gfx_countdowngradient", 306, 123, 1.25f, 1.25f);
        renderer_.number(resultBeep_, 384, 190, 1, true);
        renderer_.image("txt_end_life", 0, promptY);
        renderer_.image("txt_end_life_reflection", 0, 276);
        renderer_.text("X: next wish", 240, 266, 11, 0xffffffff, true);
    }
    renderer_.image("btn_settings_up", 322, 251);
    if (!save_.writable) renderer_.text("Save unavailable - progress kept for this session", 240, 316, 9, rgba(255, 220, 150), true);
}
void Game::drawButton(const std::string& label, int index, float x, float y, float w) {
    uint32_t border = focus_ == index ? rgba(255, 230, 125) : rgba(180, 116, 225, 150);
    renderer_.rect(x - 2, y - 2, w + 4, 34, border);
    renderer_.rect(x, y, w, 30, rgba(33, 10, 66, 240));
    renderer_.text(label, x + w / 2, y + 21, 13, 0xffffffff, true);
}
void Game::drawMenu() {
    if (scene_ == Scene::Title) {
        renderer_.image("ititle", 0, 0);
        centered("touch_to_play", 264, 1);
        renderer_.sprite("sparkle_over_text", int(uiTime_ * 20) % 15, 3, 5, 103, 257);
        if (focus_ == 0) renderer_.text("X: PLAY", 240, 258, 11, rgba(255, 243, 148), true);
        drawButton("Settings", 1, 10, 10, 100);
        drawButton("High scores", 2, 360, 10, 110);
        drawButton("How to play", 3, 10, 48, 100);
        drawButton("Milestones", 4, 360, 48, 110);
        return;
    }
    renderer_.image("menu_settings", 0, 0);
    renderer_.image("screen_container", 0, 0);
    if (scene_ == Scene::Pause) {
        renderer_.text("PAUSED", 240, 60, 24, 0xffffffff, true);
        drawButton("Resume", 0, 145, 94);
        drawButton("Settings", 1, 145, 134);
        drawButton("How to play", 2, 145, 174);
        drawButton("Return to title", 3, 145, 214);
        renderer_.text("START / O: resume", 240, 284, 12, 0xffffffff, true);
    } else if (scene_ == Scene::Settings) {
        renderer_.text("SETTINGS", 240, 49, 22, 0xffffffff, true);
        drawButton(std::string("Music: ") + (save_.music ? "ON" : "OFF"), 0, 110, 72, 260);
        drawButton(std::string("Sound effects: ") + (save_.effects ? "ON" : "OFF"), 1, 110, 110, 260);
        drawButton(std::string("Touch: ") + (save_.touchButtons ? "BUTTONS" : "GESTURES"), 2, 110, 148, 260);
        drawButton("Original credits", 3, 110, 186, 260);
        drawButton("Done", 4, 110, 224, 260);
        renderer_.text(save_.writable ? "D-pad: select   X: change   O: back" : "Save unavailable - settings are temporary", 240, 288, 11, 0xffffffff, true);
    } else if (scene_ == Scene::Credits) {
        static const int pages[] = {2, 1, 3};
        renderer_.image("txt_credits_page" + std::to_string(pages[creditPage_]), 0, 0);
        renderer_.image("btn_prev_up", 44, 268);
        renderer_.image("btn_done_up", 177, 268);
        renderer_.image("btn_next_up", 336, 268);
    } else if (scene_ == Scene::Help) {
        renderer_.text("MAKE YOUR WISH", 240, 47, 22, 0xffffffff, true);
        renderer_.sprite("run", int(uiTime_ * 45) % 36, 9, 4, 196, 64);
        renderer_.text("X or L: jump. Press again to double jump.", 240, 137, 13, 0xffffffff, true);
        renderer_.text("Hold for height; release for a shorter jump.", 240, 165, 13, 0xffffffff, true);
        renderer_.text("Square, O or R: rainbow dash through stars.", 240, 193, 13, 0xffffffff, true);
        renderer_.text("Collect fairies and smash stars for chain bonuses.", 240, 221, 12, 0xffffffff, true);
        renderer_.text("Touch left to jump, right to dash. START pauses.", 240, 249, 12, 0xffffffff, true);
        renderer_.text("You get three wishes. Make them count.", 240, 278, 13, rgba(255, 232, 129), true);
        renderer_.text("X / O: back", 240, 307, 11, 0xffffffff, true);
    } else if (scene_ == Scene::Scores) {
        renderer_.image("high_scores", 0, 0);
        for (int i = 0; i < 5; ++i) renderer_.number(javaInt(save_.best[i]), 240, 50 + i * 30);
        renderer_.text("Local high scores - total of three wishes", 240, 225, 12, 0xffffffff, true);
        renderer_.text("Games: " + std::to_string(save_.games) + "    Stars: " + std::to_string(save_.stars), 240, 252, 12, 0xffffffff, true);
        renderer_.text("Fairies: " + std::to_string(save_.fairies), 240, 276, 12, 0xffffffff, true);
        renderer_.text("X / O: back", 240, 307, 11, 0xffffffff, true);
    } else if (scene_ == Scene::Achievements) {
        renderer_.text("MILESTONES", 240, 42, 22, 0xffffffff, true);
        static const char* labels[] = {"Collect 100 fairies", "Collect 1,000 fairies", "Collect 2,000 fairies",
            "Smash 10 stars in a row", "Smash 20 stars in a row", "Smash 30 stars in a row",
            "10,000 points in one wish", "20,000 points in one wish", "30,000 points in one wish",
            "Smash 50 stars", "Smash 500 stars", "Smash 2,000 stars",
            "Finish one game", "Finish 25 games", "Finish 100 games", "Finish 250 games",
            "30,000 points in three wishes", "50,000 points in three wishes", "80,000 points in three wishes",
            "Earn 10,000,000 lifetime points", "Spend 300 seconds airborne in one wish",
            "Reach a wave of three dolphins", "Reach a wave of seven dolphins", "10,000 points without smashing a star"};
        for (int row = 0; row < 8; ++row) {
            const int i = focus_ * 8 + row;
            bool unlocked = save_.achievements & (uint32_t(1) << i);
            renderer_.text(std::string(unlocked ? "*  " : "-  ") + labels[i], 55, 76 + row * 25, 11,
                           unlocked ? rgba(255, 237, 129) : rgba(211, 191, 230));
        }
        renderer_.text("D-pad: page " + std::to_string(focus_ + 1) + "/3   X / O: back", 240, 307, 11, 0xffffffff, true);
    }
}
void Game::draw() {
    renderer_.begin(scene_ == Scene::Playing || scene_ == Scene::Dying ? rgba(191, 231, 255) : rgba(0, 0, 0));
    if (scene_ == Scene::Transition) {
        const int shade = transitionTick_ >= 4 ? std::min(255, (transitionTick_ - 3) * 4) : 0;
        renderer_.rect(0, 0, 480, 320, rgba(shade, shade, shade));
    } else if (scene_ == Scene::Playing || scene_ == Scene::Dying) drawWorld();
    else if (scene_ == Scene::Intro) {
        renderer_.rect(0, 0, 480, 320, rgba(70, 0, 116));
        int frame = introFrame_;
        if (frame < 54) {
            const auto& point = introPositions[frame];
            renderer_.image("inter_" + std::to_string(frame), point.x, point.y);
        }
        centered("you_get_three_wishes", 12);
        centered("txt_interstitial_" + std::to_string(quote_), 261);
    } else if (scene_ == Scene::LifeOver || scene_ == Scene::GameOver) drawResults(scene_ == Scene::GameOver);
    else drawMenu();
    if (entryFadeVisible_ && (scene_ == Scene::Intro || scene_ == Scene::LifeOver || scene_ == Scene::GameOver))
        renderer_.sprite("fad_anim", entryFade_.frame, 4, 1, 0, 0, 4);
    renderer_.end();
}
}
