#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rua {
void Animation::tick(int frames, double multiplier, bool loop, double fps) {
    elapsed += 0.03333333333333333;
    const double interval = multiplier > 0 ? 1.0 / (fps * multiplier) : 0;
    if (elapsed >= interval) {
        frame += interval != 0 ? int(elapsed / interval + 0.5) : 1;
        elapsed = 0;
    }
    if (frame >= frames) {
        if (loop) frame = 0;
        else { frame = frames - 1; done = true; }
    }
}
Game::Game(Assets& assets, Renderer& renderer, Audio& audio, Save& save, uint64_t seed)
    : assets_(assets), renderer_(renderer), audio_(audio), save_(save), random_(seed), quoteRandom_(seed) {
    audio_.settings(save_.music, save_.effects);
    audio_.music(Audio::Track::Opening, true);
}
int Game::random(int bound) { return random_.nextInt(bound); }
void Game::change(Scene scene) { scene_ = scene; focus_ = 0; }
void Game::advanceWall(double seconds) {
    // java.util.Timer deadlines are independent of the bounded simulation catch-up.
    if (scene_ == Scene::Pause || scene_ == Scene::Settings || scene_ == Scene::Help || scene_ == Scene::Scores) return;
    wallTime_ += seconds;
    if (scene_ == Scene::Intro && introDeadline_ >= 0 && wallTime_ >= introDeadline_) {
        introReady_ = true; introDeadline_ = -1;
    }
    if ((scene_ == Scene::LifeOver || scene_ == Scene::GameOver) && promptDeadline_ >= 0 && wallTime_ >= promptDeadline_) {
        promptBounceTick_ = 0; promptSparkleVisible_ = true; resultPromptSparkle_.reset();
        promptDeadline_ = wallTime_ + 2;
    }
    if (scene_ == Scene::LifeOver && countdownDeadline_ >= 0 && wallTime_ >= countdownDeadline_) {
        if (resultBeep_ > 0) { --resultBeep_; audio_.play("scoremenutimerbeep"); countdownDeadline_ = wallTime_ + 1; }
        else { resultReady_ = true; countdownDeadline_ = -1; }
    }
}
void Game::transitionTo(Scene scene) {
    transitionTarget_ = scene; transitionTick_ = 0;
    transitionLength_ = scene == Scene::Intro && !introSeen_ ? 3 : 68;
    introDeadline_ = countdownDeadline_ = promptDeadline_ = -1;
    change(Scene::Transition);
}
void Game::startEntryFade() {
    entryFadeVisible_ = true; entryFade_.reset(); entryFade_.tick(4, 1, false, 12);
}
void Game::newGame() {
    scores_.fill(0); wish_ = 0; bestRank_ = -1;
    audio_.pause(false); audio_.stopEffects(); audio_.music(Audio::Track::Silence);
    audio_.play("unicornintrosequence");
    transitionTo(Scene::Intro);
}
void Game::initializeIntro() {
    introFrame_ = 0; introMusic_ = introReady_ = false;
    quote_ = quoteRandom_.nextInt(4);
    entryFadeVisible_ = false;
    if (introSeen_) startEntryFade();
    introSeen_ = true;
    change(Scene::Intro);
}
void Game::startWish() {
    // A new wish preserves the previous death cause and dash visual state.
    double offset = player_.dashOffset, direction = player_.dashDirection, wait = player_.dashWait;
    const Rect previousCollider = player_.collider;
    const bool previousRainbow = player_.rainbow;
    player_ = Player{}; player_.collider = previousCollider; player_.rainbow = previousRainbow;
    player_.dashOffset = offset; player_.dashDirection = direction; player_.dashWait = wait;
    entities_.clear(); effects_.clear(); popups_.assign(6, {}); dolphins_.clear(); objectOrder_ = 0;
    for (auto& popup : popups_) popup.alive = false;
    cameraY_ = cameraX_ = 0; cameraFall_ = cameraGain_ = shakeTime_ = shakePhase_ = 0;
    shaking_ = hintTriggered_ = false; nearestStar_ = 2147483647.0;
    clouds_ = 0; hints_ = dolphinMilestone_ = 0; terrainLevel_ = 3;
    dashNotice_ = {}; startNotice(wishNotice_, 2);
    previousTouchAction_ = 1; previousTouchX_ = 0;
    tiles_[0] = {0, 0, -6, 148, {}};
    tiles_[1] = {21, 0, 1018, 244, {}};
    addObjects(tiles_[0], true); addObjects(tiles_[1], true);
    for (auto& tile : tiles_) updateTileCollider(tile);
    audio_.pause(false); audio_.stopEffects(); audio_.music(Audio::Track::Game);
    audio_.play("unicornrunhooves");
    capturePlayerPose(); updateAnimations(); startEntryFade();
    change(Scene::Playing);
}
void Game::addObjects(const Tile& tile, bool first) {
    const auto& def = terrainDefs[tile.type];
    const Point fairy = def.fairies[first ? 1 : 0];
    // Match vector operation order and Java integer half-width, including dt=0 update.
    float fx = (fairy.x + tile.x) - def.crop.x;
    float fy = (fairy.y + tile.y) - def.crop.y;
    fx += 10; fx += -31; fy += -20;
    fy = float(double(fy) + std::sin(96.0) * 1.5);
    entities_.push_back({false, true, false, fx, fy, 96, {}});
    entities_.back().order = objectOrder_++;
    if (first) return;
    const int index = random(4);
    if (index >= 2 || def.stars[index].x == 0 || def.stars[index].x == -1) return;
    float sx = (-41.0f + (def.stars[index].x + tile.x)) - def.crop.x;
    float sy = (-38.0f + (def.stars[index].y + tile.y)) - def.crop.y;
    Entity star{true, true, sx <= 640, sx, sy, 90, {}};
    if (star.announced) audio_.play("star");
    nearestStar_ = std::min(nearestStar_, double(sx - cameraX_));
    star.order = objectOrder_++; entities_.push_back(star);
}
void Game::updateTileCollider(Tile& tile) {
    const auto& texture = assets_.get("platform" + std::to_string(tile.type + 1));
    tile.collider = {javaCast(tile.x), javaCast(tile.y), javaCast(tile.x) + texture.width,
                     javaCast(tile.y) + texture.height};
}
void Game::recycleTerrain() {
    if (tiles_[1].x > 0) return;
    const Tile previous = tiles_[1];
    const Rect recycledRect = tiles_[0].collider;
    const auto& node = nodes[previous.node];
    std::array<int, 8> choices{}; int count = 0, easiest = 0, bestDifficulty = 9999;
    for (int i = 0; i < node.count; ++i) {
        const int value = difficulty[nodes[node.edges[i].node].tile];
        if (value < bestDifficulty) { bestDifficulty = value; easiest = i; }
        if (value <= terrainLevel_) choices[count++] = i;
    }
    const auto& edge = node.edges[count ? choices[random(count)] : easiest];
    const int next = nodes[edge.node].tile;
    const double naturalGap = javaCast(1024.0f - terrainDefs[previous.type].end.x) +
                              double(javaCast(terrainDefs[next].start.x));
    const double desiredGap = javaMultiply(javaCast(player_.speed), 66);
    const float extra = float(desiredGap - std::min(desiredGap, naturalGap));
    tiles_[0] = previous;
    // Position assignment does not refresh the recycled object's Rect until world update.
    tiles_[1] = {edge.node, next, (1024.0f - 50.0f + extra) + terrainDefs[next].crop.x,
                  float(-edge.rise - 290) + terrainDefs[next].crop.y, recycledRect};
    addObjects(tiles_[1], false);
}
void Game::setMotion(Motion motion) {
    auto& p = player_;
    const int old = p.fallLoop ? 7 : p.motion == Motion::DoubleJump ? 1 : int(p.motion);
    if (old < 8) p.angles[old] = p.angle;
    p.motion = motion; p.animation.reset(); p.fallLoop = false;
    const int index = motion == Motion::DoubleJump ? 1 : int(motion);
    p.angle = index < 8 ? p.angles[index] : 0;
    p.animationRate = motion == Motion::Jump || motion == Motion::DoubleJump ? 0.4 : motion == Motion::Fall ? 0.5 : 1;
}
int Game::frameHeight() const {
    switch (player_.motion) {
        case Motion::Jump: case Motion::DoubleJump: case Motion::Fall: return 61;
        case Motion::Dash: return 90;
        case Motion::Head: case Motion::Dead: return 111;
        default: return 48;
    }
}
void Game::capturePlayerPose() {
    auto& p = player_;
    const int index = p.fallLoop ? 7 : p.motion == Motion::DoubleJump ? 1 : p.motion == Motion::Dead ? 6 : int(p.motion);
    p.poses[index] = {p.x, p.y - frameHeight() + 10};
    if (p.motion == Motion::Dash) {
        p.poses[index].x = (p.x - 100.0f) + float(p.dashOffset);
        p.poses[index].y += 21;
    }
}
void Game::rebuildCollider() {
    const auto& p = player_;
    player_.collider = {javaCast(p.x) - (p.motion == Motion::Dash ? 100 : 0),
        javaCast(p.y) - frameHeight() + 10 + (p.motion == Motion::Dash ? 42 : 0), 88, javaCast(p.y)};
}
void Game::correctFeet(int y) {
    auto& p = player_;
    p.collider.top -= javaCast(p.y) - y; p.collider.bottom = y; p.y = float(y); p.vy = 0;
}
int Game::terrainAlpha(const Tile& tile, float x, float y) const {
    return assets_.get("platform" + std::to_string(tile.type + 1)).pixel(javaCast(-tile.x + x), javaCast(y + -tile.y));
}
bool Game::solid(const Tile& tile, float x, float y) const { return terrainAlpha(tile, x, y) == 255; }
bool Game::overlapsTile(const Tile& tile) const { return player_.collider.intersects(tile.collider); }
void Game::collideTerrain() {
    for (const auto& tile : tiles_) {
        if (player_.motion == Motion::Head || player_.motion == Motion::Dead) return;
        if (overlapsTile(tile)) collideTile(tile);
    }
}
void Game::collideTile(const Tile& tile) {
    auto& p = player_; auto& rect = p.collider;
    const bool airborne = p.motion == Motion::Jump || p.motion == Motion::DoubleJump || p.motion == Motion::Fall;
    const bool dashing = p.motion == Motion::Dash;
    if (airborne || dashing) {
        const int upper = terrainAlpha(tile, float(rect.right), float(rect.bottom - 38));
        const int lower = terrainAlpha(tile, float(rect.right), float(rect.bottom));
        if ((upper != 0 && upper != 255) || (lower != 0 && lower != 255 && upper != 255)) { die(2); return; }
    }
    if (airborne) {
        p.ceiling = false;
        float y = float(rect.bottom - 38);
        if (solid(tile, float(rect.right), y)) {
            int depth = 0;
            do { ++depth; y += 1; if (depth > 29) break; } while (solid(tile, float(rect.right), y));
            if (depth > 29) { die(2); return; }
            correctFeet(javaCast(y) + 38); p.ceiling = true;
        } else if (solid(tile, float(rect.right), float(rect.bottom))) {
            setMotion(Motion::Land);
            int y = rect.bottom;
            do { --y; } while (solid(tile, float(rect.right), float(y)));
            correctFeet(y + 1); p.jumps = p.dashes = 0; p.cooldown = 0; p.released = false;
        }
        return;
    }
    if (p.motion != Motion::Run && !dashing) return;
    if (solid(tile, float(rect.right), float(rect.bottom))) {
        if (p.ceiling) { die(2); return; }
        int y = rect.bottom, depth = 0;
        do { --y; ++depth; if (depth >= 29) break; } while (solid(tile, float(rect.right), float(y)));
        if (depth >= 29) { die(2); return; }
        if (p.motion == Motion::Run) ++y;
        correctFeet(y);
        int front = rect.bottom, n = 0; bool hit;
        do { --front; hit = solid(tile, float(rect.right), float(front)); ++n; if (n >= 35) break; } while (hit);
        if (n < 35) p.angle = float(std::atan2(double(rect.right - rect.left), double(rect.bottom - (front + 1))) / 3.141592653589793 * 180.0 - 90.0);
    } else if (p.motion == Motion::Run) {
        int y = rect.bottom, n = 0; bool hit;
        do { ++y; hit = solid(tile, 44, float(y)); ++n; if (n >= 62) break; } while (!hit);
        if (n < 62) correctFeet(y);
        int front = rect.bottom; n = 0;
        do { ++front; hit = solid(tile, float(rect.right), float(front)); ++n; if (n >= 35) break; } while (!hit);
        if (n < 35) p.angle = float(std::atan2(double(rect.right - rect.left), double(rect.bottom - front)) / 3.141592653589793 * 180.0 - 90.0);
        else {
            setMotion(Motion::Fall); p.released = p.ledgeFall = true;
            p.jumps = p.dashes = 0; p.cooldown = 0;
            trail("rainbow_fall_fader", 12, 12, 0.5f); audio_.stopLatest("unicornrunhooves");
            return;
        }
    }
    if (dashing) {
        p.ceiling = false; float y = float(rect.bottom - 38);
        if (solid(tile, float(rect.right), y)) {
            int depth = 0;
            do { ++depth; y += 1; if (depth > 29) break; } while (solid(tile, float(rect.right), y));
            if (depth > 29) { die(2); return; }
            correctFeet(javaCast(y) + 38); p.ceiling = true;
        }
    }
}
void Game::effect(const std::string& name, int columns, int rows, int frames, float x, float y, double fps, double multiplier) {
    // Android owns one reusable effect per name, not an unbounded set of instances.
    for (auto& e : effects_) if (e.image == name) {
        e.x = x; e.y = y; e.visible = true; e.animation.reset(); return;
    }
    effects_.push_back({name, columns, rows, frames, x, y, fps, multiplier, {}, true});
}
void Game::hideEffect(const std::string& name) {
    for (auto& e : effects_) if (e.image == name) { e.visible = false; e.animation.reset(); }
}
void Game::stopTrail() {
    for (auto& e : effects_) if (e.image.find("rainbow_") == 0 || e.image == "tail_double_jump") {
        e.visible = false; e.animation.reset();
    }
}
void Game::trail(const std::string& name, int columns, int frames, double rate) {
    effect(name, columns, name == "tail_double_jump" ? 2 : 1, frames,
        player_.x - (name == "tail_double_jump" ? 30 : 0),
        player_.y - frameHeight() - (name == "tail_double_jump" ? 6 : 0), 30, rate);
}
void Game::jump() {
    auto& p = player_;
    if (p.motion == Motion::Head || p.motion == Motion::Dead || p.jumps >= 2) return;
    audio_.stopLatest("unicornrunhooves");
    p.released = false;
    if (p.jumps == 1) {
        hideEffect("rainbow_jump"); hideEffect("rainbow_fall"); hideEffect("rainbow_fall_fader");
        p.rainbow = false; setMotion(Motion::DoubleJump); trail("tail_double_jump", 4, 7, 0.5f);
        audio_.stopLatest("unicornjumpsparkle"); audio_.play("unicorndoublejumpharp");
    } else {
        p.rainbow = true; setMotion(Motion::Jump); trail("rainbow_jump", 9, 9, 0.4);
        for (auto& e : effects_) if (e.image == "rainbow_jump") e.multiplier = 0.4;
        audio_.play("unicornjumpsparkle");
    }
    p.angle = 0; p.vy = -440; ++p.jumps; capturePlayerPose();
}
void Game::releaseJump() {
    auto& p = player_;
    if (p.motion != Motion::Jump && p.motion != Motion::DoubleJump && p.motion != Motion::Fall) return;
    p.released = true; p.angle = 0; p.animationRate = 0;
    if (p.fallLoop) p.fallLoopRate = 0;
    if (p.vy < -100) p.vy += (-p.vy) * 0.25;
    for (auto& e : effects_) if ((p.motion == Motion::Jump && e.image == "rainbow_jump") ||
                                (p.motion == Motion::Fall && e.image == "rainbow_fall")) e.multiplier = 0;
}
void Game::dash() {
    auto& p = player_;
    p.dashDirection = 0.5; p.dashWait = p.dashOffset = 0;
    if (p.motion == Motion::Head || p.motion == Motion::Dead || p.dashes >= 3 || p.cooldown > 0) return;
    stopTrail(); audio_.stopLatest("unicornrunhooves"); setMotion(Motion::Dash);
    p.cooldown = p.dashes * 1000 + 750; ++p.dashes;
    if (p.jumps == 2) --p.jumps;
    p.vy = 0; capturePlayerPose(); audio_.play("unicorndashwoosh");
}
void Game::die(int cause) {
    auto& p = player_;
    if (p.motion == Motion::Head || p.motion == Motion::Dead) return;
    deathCause_ = cause;
    stopTrail(); audio_.stopLatest("unicornrunhooves");
    int width = p.motion == Motion::Run || p.motion == Motion::Land || p.motion == Motion::Dash ? 88 : 83;
    const int height = p.motion == Motion::Dash ? 48 : frameHeight();
    effect("explode", 5, 2, 10, p.x - (138 - width) / 2, p.y - height - (138 - height) / 2);
    audio_.play("unicorndeathexplosion");
    setMotion(Motion::Head);
    if (cause == 0) p.y = float(javaCast(320.0f - cameraY_));
    p.speed = 0; p.vy = cause == 0 ? -475 : -575; p.headScale = 0;
    scores_[wish_] = p.score;
    save_.store(); shaking_ = true;
    change(Scene::Dying);
}
void Game::updatePlayer() {
    auto& p = player_;
    p.cooldown -= 1000.0 * dt;
    updateNotices();
    if (p.motion != Motion::Dash) p.vy += p.released ? 32 : p.motion == Motion::Head ? 32 : 16;
    if (p.motion != Motion::Head && p.motion != Motion::Dead) p.score = javaAdd(p.score, int(180.0 * dt));
    p.y = float(double(p.y) + p.vy * dt);
    if (p.motion == Motion::Head) p.x = float(double(p.x) + 85 * dt);
    if (p.y > 1024 && p.motion != Motion::Head && p.motion != Motion::Dead) die(0);
    if (p.animation.done) {
        switch (p.motion) {
            case Motion::Jump:
                setMotion(Motion::Fall); hideEffect("rainbow_jump");
                p.animationRate = p.released ? 0 : 0.5;
                if (p.rainbow) {
                    p.ledgeFall = false; trail("rainbow_fall", 12, 12, 0.5f);
                    for (auto& e : effects_) if (e.image == "rainbow_fall") e.multiplier = p.animationRate;
                }
                break;
            case Motion::DoubleJump: setMotion(Motion::Fall); p.animationRate = p.released ? 0 : 0.5; break;
            case Motion::Fall:
                p.angles[3] = p.angle; p.animation.reset(); p.fallLoop = true; p.angle = p.angles[7]; p.animationRate = p.fallLoopRate; break;
            case Motion::Land: setMotion(Motion::Run); break;
            case Motion::Dash: {
                const bool supported = overlapsTile(tiles_[0]) ? solid(tiles_[0], p.x, p.y) :
                    overlapsTile(tiles_[1]) && solid(tiles_[1], p.x, p.y);
                setMotion(supported ? Motion::Run : Motion::Fall); break;
            }
            case Motion::Head:
                p.headScale = float(double(p.headScale) + 0.019999999552965164);
                if (double(p.headScale) >= (deathCause_ == 0 ? 1.0 : 0.8)) p.motion = Motion::Dead;
                break;
            default: break;
        }
    }
    rebuildCollider(); collideTerrain();
    if (p.motion == Motion::Dash) {
        p.dashOffset = 400.0 * p.dashDirection * dt + p.dashOffset;
        if (p.dashOffset > 20) { p.dashDirection = 0; p.dashWait += dt; }
        else if (p.dashOffset < 0) { p.dashOffset = 0; p.dashDirection = 0; }
        if (p.dashWait > 0.4) p.dashDirection = -0.25;
    }
    if (p.motion == Motion::Head || p.motion == Motion::Dead) p.speed = 0;
    else {
        if (p.score / 1000 != p.speedMilestone) {
            p.speedMilestone = p.score / 1000; p.speedBonus = std::min(10.0, p.speedBonus + 0.5);
        }
        p.speed = 6 + p.speedBonus + (p.motion == Motion::Dash ? 2 : 0);
    }
    capturePlayerPose();
    // Update only the anchors touched by the original player update. Others keep scrolling.
    for (auto& e : effects_) {
        if ((p.motion == Motion::Jump && e.image == "rainbow_jump") ||
            (p.motion == Motion::Fall && e.image == (p.ledgeFall ? "rainbow_fall_fader" : "rainbow_fall"))) {
            e.x = p.x; e.y = p.y - frameHeight();
        }
        if (e.image == "tail_double_jump") { e.x = p.x - 30; e.y = p.y - frameHeight() - 6; }
    }
    if (p.motion == Motion::Jump || p.motion == Motion::DoubleJump || p.motion == Motion::Fall) {
        p.airborneTime += dt;
        if (p.airborneTime >= 300 && !p.airborneAwarded) { p.airborneAwarded = true; achievement(20); }
    }
    if (p.motion == Motion::Run) {
        if (p.animation.frame >= 5 && !p.hoofPlayed) { p.hoofPlayed = true; audio_.stopLatest("unicornrunhooves"); audio_.play("unicornrunhooves"); }
        else if (p.animation.frame < 5) p.hoofPlayed = false;
    }
}
void Game::updateEntities() {
    auto& p = player_;
    for (auto& e : entities_) {
        if (!e.alive) continue;
        if (!e.star) {
            e.phase += 6; if (e.phase > 360) e.phase = 0;
            e.x = float(double(e.x) - p.speed * dt * 60.0);
            e.y = float(double(e.y) + std::sin(e.phase) * 1.5);
        } else {
            e.x = float(double(e.x) - p.speed * dt * 60.0);
            if (!e.announced && e.x <= 640) { e.announced = true; audio_.play("star"); }
            nearestStar_ = std::min(nearestStar_, double(e.x - cameraX_));
        }
        Rect rect = p.collider;
        const Rect object{javaCast(e.x), javaCast(e.y), javaCast(e.x) + (e.star ? 83 : 63), javaCast(e.y) + (e.star ? 76 : 40)};
        if (!e.star || p.motion != Motion::Dash) { rect.right -= 10; rect.bottom -= 10; }
        if (!e.star) rect.top -= 15;
        bool hit = p.motion != Motion::Head && rect.intersects(object);
        if (hit && e.star && p.motion != Motion::Dash) {
            const auto& texture = assets_.get("star");
            hit = texture.pixel(rect.right - javaCast(e.x), rect.top - javaCast(e.y)) == 255 ||
                  texture.pixel(rect.right - javaCast(e.x), rect.bottom - javaCast(e.y)) == 255 ||
                  texture.pixel(rect.left - javaCast(e.x), rect.bottom - javaCast(e.y)) == 255;
        }
        if (hit) {
            if (e.star && p.motion != Motion::Dash) {
                if (p.motion != Motion::Dead) {
                    audio_.play("unicornhitstar"); e.struck = true;
                    die(deathCause_); deathCause_ = 1; // Preserve the original call-before-cause assignment.
                }
            } else {
                const int chain = e.star ? p.starChain : p.fairyChain;
                if (e.star) p.starChain = javaAdd(p.starChain, 1); else p.fairyChain = javaAdd(p.fairyChain, 1);
                const int value = javaMultiply(chain, e.star ? 100 : 10);
                p.score = javaAdd(p.score, value);
                auto slot = std::find_if(popups_.begin(), popups_.end(), [](const Popup& popup) { return !popup.alive; });
                if (slot == popups_.end()) { popups_.emplace_back(); slot = popups_.end() - 1; }
                // Reuse popup slots without resetting their sound latch.
                const bool sounded = slot->sounded;
                *slot = {value, !e.star, sounded, e.x + cameraX_, e.y + cameraY_, 1, cameraX_, cameraY_, {}, {}, false, true};
                if (e.star) {
                    effect("star_explodes", 4, 2, 8, e.x - (192 - 83) / 2 + 22, e.y - (200 - 76) / 2 + 22);
                    audio_.play("unicornkillstarexplosion"); save_.stars = uint32_t(javaAdd(javaInt(save_.stars), 1)); shaking_ = true;
                    p.noStarAwarded = true;
                    if (save_.stars == 50) achievement(9);
                    if (save_.stars == 500) achievement(10);
                    if (save_.stars == 2000) achievement(11);
                    if (chain == 10) achievement(3);
                    if (chain == 20) achievement(4);
                    if (chain == 30) achievement(5);
                } else {
                    effect("sparkle_effect", 3, 2, 6, float(double(e.x + 31) - 47.5), float(double(e.y + 20) - 47.5));
                    audio_.play("unicornhitfaerie"); save_.fairies = uint32_t(javaAdd(javaInt(save_.fairies), 1));
                    if (save_.fairies == 100) achievement(0);
                    if (save_.fairies == 1000) achievement(1);
                    if (save_.fairies == 2000) achievement(2);
                }
                e.alive = false;
            }
        }
        if (hit && e.star) continue; // Original star update returns immediately after a hit.
        if (object.right < 0) { if (e.star) p.starChain = 1; else p.fairyChain = 1; e.alive = false; }
    }
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [](const Entity& e) { return !e.alive; }), entities_.end());
}
void Game::updateEffects() {
    for (auto& e : effects_) {
        e.x = float(double(e.x) - player_.speed * dt * 60.0);
        if (e.animation.done) e.visible = false;
    }
    for (auto& popup : popups_) {
        if (!popup.alive) continue;
        if (popup.time <= 0) {
            popup.time = 0;
            if (!popup.sparkle.done) {
                if (!popup.sounded) { popup.sounded = true; audio_.play(popup.fairy ? "pointsfaeriesparkle" : "pointsstarsparkle"); }
            } else popup.alive = false;
        } else {
            popup.time -= dt;
            // Target follows camera displacement since popup creation.
            const float targetX = float(240.0 + double(cameraX_ - popup.cameraX));
            const float targetY = float(160.0 + double(cameraY_ - popup.cameraY));
            const float dx = targetX - popup.x, dy = targetY - popup.y;
            popup.x += float(double(dx * 2.0f) * dt); popup.y += float(double(dy * 2.0f) * dt);
            if (popup.time <= 0) { popup.finishing = true; popup.sparkle.reset(); popup.circle.reset(); }
        }
    }
}
void Game::updateCamera() {
    if (player_.motion != Motion::Head && player_.motion != Motion::Dead) {
        const double difference = (160.0f - player_.y) - cameraY_;
        double movement;
        if (player_.vy <= 0 || player_.motion == Motion::Dash) { cameraFall_ = cameraGain_ = 0; movement = difference * 2 * dt; }
        else { cameraFall_ += 2; cameraGain_ = std::min(6.0, cameraGain_ + 0.25); movement = (difference - cameraFall_) * cameraGain_ * dt; }
        cameraY_ = float(double(cameraY_) + movement);
        if (cameraY_ < -544) cameraY_ = -544;
    }
    if (shaking_) {
        shakeTime_ += dt;
        if (shakeTime_ > 1) { shaking_ = false; shakeTime_ = shakePhase_ = 0; return; }
        shakePhase_ += 100; if (shakePhase_ >= 360) shakePhase_ = 0;
        const double phase = shakePhase_ >= 180 ? -(shakePhase_ - 180) : shakePhase_;
        const float offset = float(std::sin(phase / 180.0 * 3.141592653589793) * 8);
        cameraY_ += offset;
        if (player_.motion == Motion::Head) cameraX_ += offset;
    }
}
void Game::spawnDolphins() {
    const int milestone = player_.score / 5000;
    if (milestone == dolphinMilestone_) return;
    dolphinMilestone_ = milestone;
    if (milestone == 3) achievement(21); else if (milestone == 7) achievement(22);
    for (int i = 0; i < milestone; ++i) {
        const float x = i % 3 == 0 ? 0 : i % 3 == 1 ? -36 : 36;
        dolphins_.push_back({x, float(320 + javaCast(-cameraY_)), -4, -30, 0.5 * i, true, objectOrder_++});
        audio_.play(random_.nextFloat() > 0.5 ? "dolphin1" : "dolphin2");
    }
}
void Game::updateDolphins() {
    for (auto& d : dolphins_) {
        if (d.delay > 0) { d.delay -= dt; if (d.delay > 0) continue; d.y = float(320 + javaCast(-cameraY_)); }
        d.x += float(140.0f * dt);
        if (d.vy >= 0) { d.vy += 12; d.y += float(d.vy * dt); d.angle = std::min(30.0f, d.vy / 4); }
        else {
            float distance = (240.0f - cameraY_) - d.y;
            if (distance < -8) { d.y += -float(double(distance * -4.0f) * dt); d.angle = std::max(-30.0f, distance); }
            else { d.vy = 0; d.angle = 0; }
        }
        if (d.y > 320.0f - cameraY_ && d.vy != -4) d.vy = -4;
        if (d.x > 480) d.alive = false;
    }
    dolphins_.erase(std::remove_if(dolphins_.begin(), dolphins_.end(), [](const Dolphin& d) { return !d.alive; }), dolphins_.end());
}
void Game::startNotice(Notice& notice, double duration) {
    notice = {}; notice.active = notice.textVisible = notice.sparkleVisible = true;
    notice.circleVisible = duration == 1; notice.remaining = duration;
    const auto& texture = assets_.get(duration == 1 ? "dash_now" : "wish" + std::to_string(3 - wish_));
    notice.x = float((480 - texture.width) / 2); notice.y = float((320 - texture.height) / 2);
}
void Game::updateNotices() {
    if (wishNotice_.active && wishNotice_.sparkle.done) {
        wishNotice_.sparkleVisible = false; wishNotice_.remaining -= dt;
        if (wishNotice_.remaining > 0) { if (wishNotice_.remaining <= 1) wishNotice_.circleVisible = true; }
        else wishNotice_.active = wishNotice_.textVisible = wishNotice_.circleVisible = false;
    }
    if (nearestStar_ < -256) { nearestStar_ = 2147483647.0; hintTriggered_ = false; }
    if (!hintTriggered_ && nearestStar_ < 480 && hints_ < 3) {
        startNotice(dashNotice_, 1); hintTriggered_ = true; ++hints_;
    }
    if (dashNotice_.active && dashNotice_.sparkle.done) {
        dashNotice_.sparkleVisible = false;
        if (dashNotice_.remaining != 0) {
            dashNotice_.remaining -= dt;
            dashNotice_.textVisible = ((int(dashNotice_.remaining * 10) % 10) % 4) <= 1;
            if (dashNotice_.remaining <= 0) dashNotice_.remaining = 0;
        } else {
            dashNotice_.textVisible = true; dashNotice_.scale -= 0.05f;
            dashNotice_.x = float(double(dashNotice_.x) + 0.10000000149011612);
            dashNotice_.y = float(double(dashNotice_.y) + 0.10000000149011612);
            if (dashNotice_.scale <= 0.2f) dashNotice_.active = dashNotice_.textVisible = dashNotice_.circleVisible = false;
        }
    }
}
void Game::updateAnimations() {
    auto& p = player_;
    switch (p.motion) {
        case Motion::Run: p.animation.tick(36, 1.5 + p.speedBonus / 10, true); break;
        case Motion::Jump: case Motion::DoubleJump: p.animation.tick(9, p.animationRate, false); break;
        case Motion::Fall: p.animation.tick(p.fallLoop ? 10 : 12, p.animationRate, p.fallLoop); break;
        case Motion::Land: case Motion::Head: case Motion::Dead: p.animation.tick(1, 1, false); break;
        case Motion::Dash: p.animation.tick(14, 1, false); break;
    }
    for (auto& e : entities_) if (!e.star) e.animation.tick(4, 0.3, true);
    for (auto& e : effects_) if (e.visible) e.animation.tick(e.frames, e.multiplier, false, e.fps);
    for (auto& popup : popups_) if (popup.alive && popup.finishing) { popup.sparkle.tick(6, 1, false, 15); popup.circle.tick(4, 1, false, 4); }
    for (Notice* n : {&wishNotice_, &dashNotice_}) {
        if (n->sparkleVisible) n->sparkle.tick(6, 1, false, 15);
        if (n->circleVisible) n->circle.tick(4, 1, false, 4);
    }
}
void Game::beginResults() {
    if (player_.score >= 10000) achievement(6);
    if (player_.score >= 20000) achievement(7);
    if (player_.score >= 30000) achievement(8);
    resultTime_ = 0; resultBeep_ = 8;
    audio_.stopEffects();
    resultReady_ = false; countdownDeadline_ = -1;
    promptDeadline_ = wallTime_ + 2; promptBounceTick_ = 10; promptSparkleVisible_ = false; startEntryFade();
    resultTear_.reset(); resultPulse_.reset(); resultSparkle_.reset();
    resultTear_.tick(40, 1, true, 40); resultPulse_.tick(6, 1, true, 8); resultSparkle_.tick(15, 1, true, 20);
    if (wish_ == 2) {
        const int total = javaAdd(javaAdd(scores_[0], scores_[1]), scores_[2]);
        bestRank_ = save_.record(total);
        save_.games = uint32_t(javaAdd(javaInt(save_.games), 1));
        if (save_.games == 1) achievement(12); else if (save_.games == 25) achievement(13);
        else if (save_.games == 100) achievement(14); else if (save_.games == 250) achievement(15);
        if (total >= 30000) achievement(16);
        if (total >= 50000) achievement(17);
        if (total >= 80000) achievement(18);
        const int lifetime = javaAdd(javaInt(save_.lifetimeScore), total);
        if (javaInt(save_.lifetimeScore) < 10000000 && lifetime >= 10000000) achievement(19);
        save_.lifetimeScore = uint32_t(lifetime);
        save_.store(); change(Scene::GameOver);
    } else { save_.store(); change(Scene::LifeOver); }
}
void Game::achievement(int bit) { save_.achievements |= uint32_t(1) << bit; }
void Game::suspend() {
    if (scene_ != Scene::Pause) {
        pauseReturn_ = scene_; pauseMenuReturn_ = menuReturn_; pauseFocus_ = focus_; change(Scene::Pause);
    }
    audio_.pause(true); save_.store();
}
Action Game::touchAction(const TouchEvent& event) {
    Action result = Action::None;
    if (save_.touchButtons) {
        if (event.action == 1) result = Action::Release;
        else if (previousTouchAction_ == 1) result = event.x < 240 ? Action::Jump : Action::Dash;
        if (event.action == 261 && event.secondX >= 240) result = Action::Dash;
        if (previousTouchAction_ == 2 && event.action == 2 && previousTouchX_ < 240 && event.x >= 240) result = Action::Dash;
        previousTouchX_ = event.x;
    } else {
        if (event.action == 2) {
            if (javaCast(event.x) - javaCast(previousTouchX_) >= 10) result = Action::Dash;
        } else if (event.action == 0 && previousTouchAction_ == 1) {
            result = Action::Jump; previousTouchX_ = float(javaCast(event.x));
        } else if (event.action == 1) { result = Action::Release; previousTouchX_ = 0; }
    }
    previousTouchAction_ = event.action;
    return result;
}
void Game::tick(const Input& input) {
    uiTime_ += dt;
    if (scene_ == Scene::Transition) {
        ++transitionTick_;
        if (transitionTick_ >= transitionLength_) {
            if (transitionTarget_ == Scene::Intro) initializeIntro();
            else if (transitionTarget_ == Scene::Playing) startWish();
            else beginResults();
        }
        return;
    }
    if (input.pause && (scene_ == Scene::Playing || scene_ == Scene::Dying || scene_ == Scene::Intro ||
                        scene_ == Scene::LifeOver || scene_ == Scene::GameOver)) { suspend(); return; }
    if (scene_ == Scene::Playing && input.touchPressed && input.touchX > 430 && input.touchY < 40) { suspend(); return; }
    if (scene_ == Scene::Intro) {
        if (introReady_) { transitionTo(Scene::Playing); return; }
        if (introFrame_ < 54) {
            ++introFrame_;
            if (introFrame_ == 54) introDeadline_ = wallTime_ + 1;
            if (!introMusic_ && introFrame_ >= 32) { introMusic_ = true; audio_.music(Audio::Track::Game, true); }
        }
    } else if (scene_ == Scene::Playing || scene_ == Scene::Dying) {
        if (!player_.noStarAwarded && player_.score >= 10000) { achievement(23); player_.noStarAwarded = true; }
        Action action = Action::None;
        for (const auto& event : input.events) {
            const Action next = event.isTouch ? touchAction(event.touch) : event.action;
            if (next != Action::None) action = next;
        }
        if (action == Action::Jump) jump(); else if (action == Action::Release) releaseJump(); else if (action == Action::Dash) dash();
        if (player_.motion == Motion::Dead) { transitionTo(wish_ == 2 ? Scene::GameOver : Scene::LifeOver); return; }
        spawnDolphins();
        terrainLevel_ = std::min(player_.score / 4000 + 3, 5);
        updatePlayer(); updateEntities(); updateDolphins(); updateEffects();
        const double cloudMovement = 60.0 * player_.speed * dt;
        clouds_ += javaCast(cloudMovement) / 2;
        if (clouds_ >= 2048) clouds_ -= 2048;
        for (auto& tile : tiles_) {
            tile.x = float(double(tile.x) - player_.speed * dt * 60.0);
            updateTileCollider(tile);
        }
        recycleTerrain(); updateCamera(); updateAnimations();
    } else if (scene_ == Scene::LifeOver || scene_ == Scene::GameOver) resultTick(input);
    else menuTick(input);
    if (entryFadeVisible_ && (scene_ == Scene::Playing || scene_ == Scene::Dying || scene_ == Scene::Intro || scene_ == Scene::LifeOver || scene_ == Scene::GameOver)) {
        if (entryFade_.frame == 3) {
            entryFadeVisible_ = false;
            if (scene_ == Scene::LifeOver) countdownDeadline_ = wallTime_ + 1;
        } else entryFade_.tick(4, 1, false, 12);
    }
}
bool Game::button(const Input& input, int index, float x, float y, float w, float h) const {
    return (input.confirm && focus_ == index) || (input.touchPressed && input.touchX >= x && input.touchX < x + w && input.touchY >= y && input.touchY < y + h);
}
void Game::resultTick(const Input& input) {
    const bool final = scene_ == Scene::GameOver;
    resultTime_ += dt;
    if (promptBounceTick_ < 10) ++promptBounceTick_;
    if (promptSparkleVisible_) resultPromptSparkle_.tick(6, 1, false, 30);
    resultTear_.tick(40, 1, true, 40); resultPulse_.tick(6, 1, true, 8); resultSparkle_.tick(15, 1, true, 20);
    const bool footer = input.touchPressed && input.touchX >= 0 && input.touchX < 480 && input.touchY >= 276 && input.touchY < 320;
    if (!final && (resultReady_ || input.confirm || footer)) { ++wish_; transitionTo(Scene::Playing); return; }
    if (final && (input.confirm || footer)) { newGame(); return; }
    if (input.settings || (input.touchPressed && input.touchX > 322 && input.touchY >= 251 && input.touchY < 276)) {
        menuReturn_ = scene_; change(Scene::Settings);
    } else if (input.scores || input.left || input.right) { menuReturn_ = scene_; change(Scene::Scores); }
    else if (input.cancel) { audio_.music(Audio::Track::Opening, true); change(Scene::Title); }
}
void Game::menuTick(const Input& input) {
    int count = scene_ == Scene::Title ? 5 : scene_ == Scene::Pause ? 4 : scene_ == Scene::Settings ? 5 : scene_ == Scene::Achievements ? 3 : 1;
    if (input.up) focus_ = (focus_ + count - 1) % count;
    if (input.down) focus_ = (focus_ + 1) % count;
    if (scene_ == Scene::Title) {
        if (input.cancel) { exiting_ = true; return; }
        if (button(input, 0, 108, 260, 264, 55)) { audio_.play("splashscreenclicksparkle"); newGame(); }
        else if (input.settings || button(input, 1, 10, 10, 100, 30)) { menuReturn_ = Scene::Title; change(Scene::Settings); }
        else if (input.scores || button(input, 2, 360, 10, 110, 30)) { menuReturn_ = Scene::Title; change(Scene::Scores); }
        else if (button(input, 3, 10, 48, 100, 30)) { menuReturn_ = Scene::Title; change(Scene::Help); }
        else if (button(input, 4, 360, 48, 110, 30)) { menuReturn_ = Scene::Title; change(Scene::Achievements); }
    } else if (scene_ == Scene::Pause) {
        if (input.cancel || input.pause || button(input, 0, 145, 94, 190, 30)) {
            change(pauseReturn_); menuReturn_ = pauseMenuReturn_; focus_ = pauseFocus_; audio_.pause(false);
        } else if (button(input, 1, 145, 134, 190, 30)) { menuReturn_ = Scene::Pause; change(Scene::Settings); }
        else if (button(input, 2, 145, 174, 190, 30)) { menuReturn_ = Scene::Pause; change(Scene::Help); }
        else if (button(input, 3, 145, 214, 190, 30)) {
            audio_.pause(false); audio_.stopEffects(); audio_.music(Audio::Track::Opening, true); change(Scene::Title);
        }
    } else if (scene_ == Scene::Settings) {
        bool toggle = input.confirm || input.left || input.right;
        int selection = focus_;
        if (input.touchPressed && input.touchX >= 110 && input.touchX < 370) {
            int row = int((input.touchY - 72) / 38);
            if (input.touchY >= 72 && row >= 0 && row < 5 && input.touchY < 72 + row * 38 + 30) { selection = row; toggle = true; }
        }
        if (toggle) {
            focus_ = selection;
            if (selection == 0) save_.music = !save_.music;
            else if (selection == 1) save_.effects = !save_.effects;
            else if (selection == 2) save_.touchButtons = !save_.touchButtons;
            else if (selection == 3) { creditPage_ = 0; change(Scene::Credits); }
            else { change(menuReturn_); }
            audio_.settings(save_.music, save_.effects); save_.store();
        }
        if (input.cancel) { save_.store(); change(menuReturn_); }
    } else if (scene_ == Scene::Credits) {
        if (input.left || (input.touchPressed && input.touchY > 268 && input.touchX < 172)) creditPage_ = (creditPage_ + 2) % 3;
        if (input.right || (input.touchPressed && input.touchY > 268 && input.touchX > 336)) creditPage_ = (creditPage_ + 1) % 3;
        if (input.cancel || input.confirm || (input.touchPressed && input.touchY > 268 && input.touchX >= 172 && input.touchX <= 336)) change(Scene::Settings);
    } else if (scene_ == Scene::Achievements) {
        if (input.left) focus_ = (focus_ + 2) % 3;
        if (input.right) focus_ = (focus_ + 1) % 3;
        if (input.cancel || input.confirm || input.touchPressed) change(menuReturn_);
    } else if (input.cancel || input.confirm || input.touchPressed) change(menuReturn_);
}
}
