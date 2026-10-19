#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    struct Buffered {
        bool active = false;
        bool held = false;
        bool releasePending = false;
        int age = 0;
    };
    Buffered g_buf[2];

    bool enabled()      { return Mod::get()->getSettingValue<bool>("enabled"); }
    int  windowSteps()  { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("buffer-frames")); }
    bool requireHeld()  { return Mod::get()->getSettingValue<bool>("require-held"); }
    bool playerOn(int i) {
        return Mod::get()->getSettingValue<bool>(i == 0 ? "buffer-player1" : "buffer-player2");
    }

    bool canActNow(PlayerObject* p) {
        if (!p) return false;
        return p->m_isOnGround || !p->m_touchedRings.empty();
    }

    void resetAll() { for (auto& b : g_buf) b = Buffered{}; }

    void note(std::string const& msg) {
        log::info("InputBuffer: {}", msg);
        Notification::create(msg, NotificationIcon::None, 0.6f)->show();
    }
}

class $modify(BufferGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (enabled() && button == 1) {
            int i = isPlayer1 ? 0 : 1;
            auto* p = isPlayer1 ? m_player1 : m_player2;
            auto& b = g_buf[i];

            if (down) {
                bool can = canActNow(p);
                if (playerOn(i) && windowSteps() > 0 && !can) {
                    b.active = true;
                    b.held = true;
                    b.age = 0;
                    note("early click BUFFERED");
                } else {
                    b.active = false;
                    note(can ? "click: can act now (normal)" : "click: not buffered (settings)");
                }
            } else {
                b.held = false;
                if (requireHeld() && b.active) {
                    b.active = false;
                    note("released too soon: cancelled");
                }
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
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

                if (!isHalfTick) b.age++;
                if (b.age > windowSteps()) {
                    b.active = false;
                    note("buffer EXPIRED");
                } else if (canActNow(p)) {
                    note("buffered click FIRED");
                    GJBaseGameLayer::handleButton(true, 1, isP1);
                    b.active = false;
                    if (!b.held) b.releasePending = true;
                }
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(BufferPlayLayer, PlayLayer) {
    void resetLevel() {
        resetAll();
        PlayLayer::resetLevel();
    }
};
