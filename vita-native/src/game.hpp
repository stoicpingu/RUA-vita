#pragma once
#include "assets.hpp"
#include "audio.hpp"
#include "original_data.hpp"
#include "save.hpp"
#include "original_semantics.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace rua {
enum class Action { None, Jump, Release, Dash };
struct TouchEvent { int action = 0; float x = 0, y = 0, secondX = 0; };
struct GameplayEvent { Action action = Action::None; TouchEvent touch; bool isTouch = false; };
struct Input {
    Action action = Action::None;
    std::vector<TouchEvent> touches;
    std::vector<GameplayEvent> events;
    bool confirm = false, cancel = false, pause = false, settings = false;
    bool up = false, down = false, left = false, right = false, scores = false;
    bool touchPressed = false;
    float touchX = 0, touchY = 0;
};
enum class Scene { Title, Intro, Playing, Dying, LifeOver, GameOver, Pause, Settings, Help, Credits, Scores, Achievements, Transition };
enum class Motion { Run, Jump, DoubleJump, Fall, Land, Dash, Head, Dead };
struct Animation {
    int frame = 0;
    double elapsed = 0;
    bool done = false;
    void reset() { frame = 0; elapsed = 0; done = false; }
    void tick(int frames, double multiplier, bool loop, double fps = 30);
};
struct Tile { int node = 0, type = 0; float x = 0, y = 0; Rect collider; };
struct Entity { bool star = false, alive = true, announced = false; float x = 0, y = 0; double phase = 90; Animation animation; bool struck = false; uint64_t order = 0; };
struct Effect {
    std::string image;
    int columns = 1, rows = 1, frames = 1;
    float x = 0, y = 0;
    double fps = 30, multiplier = 1;
    Animation animation;
    bool visible = true;
};
struct Popup {
    int score = 0; bool fairy = false, sounded = false;
    float x = 0, y = 0; double time = 1;
    float cameraX = 0, cameraY = 0; Animation sparkle, circle;
    bool finishing = false, alive = true;
};
struct Dolphin { float x = 0, y = 0, vy = -4, angle = -30; double delay = 0; bool alive = true; uint64_t order = 0; };
struct Notice {
    bool active = false, textVisible = false, sparkleVisible = false, circleVisible = false;
    double remaining = 0;
    float scale = 1, x = 0, y = 0;
    Animation sparkle, circle;
};
struct Player {
    Motion motion = Motion::Run;
    Animation animation;
    float x = 0, y = 160, angle = 0, headScale = 0;
    double vy = 0, speed = 0, speedBonus = 0, airborneTime = 0;
    bool airborneAwarded = false, noStarAwarded = false;
    double cooldown = 0, dashOffset = 0, dashDirection = 0, dashWait = 0, animationRate = 1, fallLoopRate = 1;
    std::array<float, 8> angles{};
    std::array<Point, 8> poses{};
    Rect collider;
    int jumps = 0, dashes = 0, score = 0, speedMilestone = 0, fairyChain = 1, starChain = 1;
    bool released = false, ceiling = false, rainbow = false, ledgeFall = false, fallLoop = false, hoofPlayed = false;
};
class Game {
public:
    Game(Assets& assets, Renderer& renderer, Audio& audio, Save& save, uint64_t seed);
    void tick(const Input& input);
    void advanceWall(double seconds);
    void draw();
    void suspend();
    bool exiting() const { return exiting_; }
private:
    static constexpr double dt = 0.03333333333333333;
    Assets& assets_;
    Renderer& renderer_;
    Audio& audio_;
    Save& save_;
    JavaRandom random_, quoteRandom_;
    Scene scene_ = Scene::Title, pauseReturn_ = Scene::Playing, menuReturn_ = Scene::Title;
    Scene pauseMenuReturn_ = Scene::Title;
    int pauseFocus_ = 0;
    int focus_ = 0, wish_ = 0, deathCause_ = 0, quote_ = 0, creditPage_ = 0, bestRank_ = -1;
    int dolphinMilestone_ = 0, hints_ = 0, resultBeep_ = 8, terrainLevel_ = 3;
    std::array<int, 3> scores_{};
    Player player_;
    std::array<Tile, 2> tiles_{};
    std::vector<Entity> entities_;
    std::vector<Effect> effects_;
    std::vector<Popup> popups_;
    std::vector<Dolphin> dolphins_;
    uint64_t objectOrder_ = 0;
    double uiTime_ = 0, resultTime_ = 0;
    float cameraY_ = 0, cameraX_ = 0;
    double cameraFall_ = 0, cameraGain_ = 0, shakeTime_ = 0, shakePhase_ = 0;
    bool shaking_ = false, hintTriggered_ = false;
    double nearestStar_ = 2147483647.0;
    Notice wishNotice_, dashNotice_;
    int introFrame_ = 0;
    double wallTime_ = 0, introDeadline_ = -1, countdownDeadline_ = -1;
    bool introReady_ = false, resultReady_ = false, introSeen_ = false, entryFadeVisible_ = false;
    Animation entryFade_, resultTear_, resultPulse_, resultSparkle_, resultPromptSparkle_;
    double promptDeadline_ = -1;
    int promptBounceTick_ = 10;
    bool promptSparkleVisible_ = false;
    Scene transitionTarget_ = Scene::Title;
    int transitionTick_ = 0, transitionLength_ = 68;
    int previousTouchAction_ = 1;
    float previousTouchX_ = 0;
    float clouds_ = 0;
    bool introMusic_ = false, exiting_ = false;
    int random(int bound);
    void change(Scene scene);
    void transitionTo(Scene scene);
    void initializeIntro();
    void startEntryFade();
    void newGame();
    void startWish();
    void addObjects(const Tile& tile, bool first);
    void recycleTerrain();
    void setMotion(Motion motion);
    void rebuildCollider();
    void capturePlayerPose();
    void correctFeet(int y);
    void updateTileCollider(Tile& tile);
    void updateAnimations();
    void updateNotices();
    void startNotice(Notice& notice, double duration);
    void spawnDolphins();
    void beginResults();
    Action touchAction(const TouchEvent& event);
    int frameHeight() const;
    int terrainAlpha(const Tile& tile, float x, float y) const;
    bool overlapsTile(const Tile& tile) const;
    bool solid(const Tile& tile, float x, float y) const;
    void collideTerrain();
    void collideTile(const Tile& tile);
    void jump();
    void releaseJump();
    void dash();
    void die(int cause);
    void updatePlayer();
    void updateEntities();
    void updateEffects();
    void updateCamera();
    void updateDolphins();
    void effect(const std::string& name, int columns, int rows, int frames, float x, float y, double fps = 15, double multiplier = 1);
    void hideEffect(const std::string& name);
    void stopTrail();
    void trail(const std::string& name, int columns, int frames, double rate);
    void achievement(int bit);
    void resultTick(const Input& input);
    void menuTick(const Input& input);
    bool button(const Input& input, int index, float x, float y, float w, float h) const;
    void drawWorld();
    void drawPlayer();
    void drawResults(bool final);
    void drawMenu();
    void drawButton(const std::string& label, int index, float x, float y, float w = 190);
    void centered(const std::string& name, float y, float scale = 1);
};
}
