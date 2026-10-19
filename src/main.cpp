#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    struct Buffered {
        bool active = false;         // a too-early click is waiting
        bool held = false;           // button is still down
        bool releasePending = false; // send a release next step (tap mode)
        int age = 0;                 // physics steps since the click
    };
    Buffered g_buf[2]; // [0] = player 1, [1] = player 2

    bool enabled()      { return Mod::get()->getSettingValue<bool>("enabled"); }
    int  windowSteps()  { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("buffer-frames")); }
    bool requireHeld()  { return Mod::get()->getSettingValue<bool>("require-held"); }
    bool playerOn(int i) {
        return Mod::get()->getSettingValue<bool>(i == 0 ? "buffer-player1" : "buffer-player2");
    }

    // The player can "use" a click right now: standing on something, or touching an orb/ring.
    bool canActNow(PlayerObject* p) {
        if (!p) return false;
        return p->m_isOnGround || !p->m_touchedRings.empty();
    }

    void resetAll() { for (auto& b : g_buf) b = Buffered{}; }
}

class $modify(BufferGameLayer, GJBaseGameLayer) {
    // Called for every button press/release (button 1 = jump).
    void handleButton(bool down, int button, bool isPlayer1) {
        if (enabled() && button == 1) {
            int i = isPlayer1 ? 0 : 1;
            auto* p = isPlayer1 ? m_player1 : m_player2;
            auto& b = g_buf[i];

            if (down) {
                // Only buffer if the click can't be used right now (i.e. it's "early").
                if (playerOn(i) && windowSteps() > 0 && !canActNow(p)) {
                    b.active = true;
                    b.held = true;
                    b.age = 0;
                } else {
                    b.active = false;
                }
            } else {
                b.held = false;
                if (requireHeld()) b.active = false; // released too soon -> cancel
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Runs once per physics step; we inject the delayed click before the game processes input.
    void processCommands(float dt) {
        if (enabled()) {
            for (int i = 0; i < 2; ++i) {
                auto& b = g_buf[i];
                auto* p = i == 0 ? m_player1 : m_player2;
                bool isP1 = (i == 0);

                if (b.releasePending) {
                    b.releasePending = false;
                    GJBaseGameLayer::handleButton(false, 1, isP1);
                }

                if (!b.active) continue;

                b.age++;
                if (b.age > windowSteps()) {
                    b.active = false;
                } else if (canActNow(p)) {
                    GJBaseGameLayer::handleButton(true, 1, isP1); // fire the buffered click
                    b.active = false;
                    if (!b.held) b.releasePending = true;
                }
            }
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

class $modify(BufferPlayLayer, PlayLayer) {
    void resetLevel() {
        resetAll();
        PlayLayer::resetLevel();
    }
};
