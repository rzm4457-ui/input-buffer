// ---- Click snapping logic (no Geode dependencies, unit-tested) ----
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace snap {

struct Pending {
    bool down;
    int button;
    bool player2;
    int64_t due; // frame on which this input is sent to the game
};

struct Decision {
    bool passNow = true;  // send the input to the game right now
    int delay = 0;        // how many frames the input was moved later
    int64_t target = 0;   // replay frame it was snapped to (when snapped)
    bool snapped = false;
};

class Snapper {
public:
    static int key(int button, bool p2) { return button * 2 + (p2 ? 1 : 0); }

    void setReplay(std::map<int, std::vector<int64_t>> downs) {
        for (auto& kv : downs) std::sort(kv.second.begin(), kv.second.end());
        m_downs = std::move(downs);
        reset();
    }
    bool hasReplay() const { return !m_downs.empty(); }

    void reset() {
        m_queue.clear();
        m_lastDue.clear();
        m_heldDelay.clear();
        m_claimed.clear();
    }

    // Called for every real click/release. `now` = current game frame.
    // window = max frames early that still gets corrected, offset = frame alignment fix.
    Decision onInput(bool down, int button, bool p2, int64_t now, int window, int offset) {
        int k = key(button, p2);
        Decision res;
        int delay = 0;

        if (down) {
            auto it = m_downs.find(k);
            if (window > 0 && it != m_downs.end()) {
                auto& v = it->second;
                // first replay click that has not passed yet
                auto lb = std::lower_bound(v.begin(), v.end(), now - offset);
                if (lb != v.end()) {
                    int64_t target = *lb + offset;
                    int64_t gap = target - now;
                    if (gap >= 1 && gap <= window && m_claimed[k].count(*lb) == 0) {
                        delay = static_cast<int>(gap);
                        m_claimed[k].insert(*lb);
                        res.snapped = true;
                        res.target = target;
                    }
                }
            }
            m_heldDelay[k] = delay;
        } else {
            // the release gets the same delay as its click, so hold length is unchanged
            auto it = m_heldDelay.find(k);
            if (it != m_heldDelay.end()) {
                delay = it->second;
                it->second = 0;
            }
        }

        // never let an input overtake an earlier delayed input of the same button
        int64_t due = now + delay;
        auto ld = m_lastDue.find(k);
        if (ld != m_lastDue.end() && ld->second > due) due = ld->second;
        m_lastDue[k] = due;

        res.delay = static_cast<int>(due - now);
        if (due <= now) {
            res.passNow = true;
            return res;
        }
        res.passNow = false;
        m_queue.push_back({down, button, p2, due});
        return res;
    }

    // Collect every queued input whose frame has arrived.
    void popDue(int64_t now, std::vector<Pending>& out) {
        for (auto it = m_queue.begin(); it != m_queue.end();) {
            if (it->due <= now) {
                out.push_back(*it);
                it = m_queue.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t queued() const { return m_queue.size(); }

private:
    std::map<int, std::vector<int64_t>> m_downs;
    std::vector<Pending> m_queue;
    std::map<int, int64_t> m_lastDue;
    std::map<int, int> m_heldDelay;
    std::map<int, std::set<int64_t>> m_claimed;
};

} // namespace snap

// ---- Geode mod ----
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <gdr/gdr.hpp>
#include <filesystem>
#include <string>

using namespace geode::prelude;

namespace {
    snap::Snapper g_snap;
    int64_t g_lastFrame = 0;
    bool g_triedLoad = false;
    bool g_replayOk = false;
    std::filesystem::path g_loadedPath;
    std::filesystem::file_time_type g_loadedTime{};

    struct LoadedReplay : gdr::Replay<LoadedReplay, gdr::Input<>> {
        LoadedReplay() : Replay("InputBuffer", 1) {}
    };

    bool enabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
    int  windowFrames() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("window")); }
    int  frameOffset() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("frame-offset")); }
    bool debug() { return Mod::get()->getSettingValue<bool>("debug"); }

    // Debug pop-up (only when the debug setting is on). Always written to the Geode log.
    void note(std::string const& msg) {
        log::info("InputBuffer: {}", msg);
        if (debug()) Notification::create(msg.c_str(), NotificationIcon::None, 0.8f)->show();
    }

    // Errors are always shown.
    void warn(std::string const& msg) {
        log::warn("InputBuffer: {}", msg);
        Notification::create(msg.c_str(), NotificationIcon::Error, 2.5f)->show();
    }

    void ensureLoaded() {
        auto path = Mod::get()->getSettingValue<std::filesystem::path>("replay-file");
        std::error_code ec;
        std::filesystem::file_time_type time{};
        if (!path.empty()) time = std::filesystem::last_write_time(path, ec);

        if (path == g_loadedPath && time == g_loadedTime) return; // nothing changed

        g_loadedPath = path;
        g_loadedTime = time;
        g_replayOk = false;
        g_snap.setReplay({});
        if (path.empty()) return;

        auto res = LoadedReplay::importData(path);
        if (res.isErr()) {
            warn("Input Buffer: could not read replay: " + std::string(res.unwrapErr()));
            return;
        }
        auto replay = std::move(res).unwrap();

        std::map<int, std::vector<int64_t>> downs;
        size_t clicks = 0;
        for (auto const& in : replay.inputs) {
            if (!in.down) continue;
            downs[snap::Snapper::key(in.button, in.player2)].push_back(static_cast<int64_t>(in.frame));
            ++clicks;
        }
        g_snap.setReplay(std::move(downs));
        g_replayOk = g_snap.hasReplay();
        if (!g_replayOk) {
            warn("Input Buffer: replay has no clicks");
            return;
        }
        if (replay.framerate != 240.0) {
            log::warn("InputBuffer: replay framerate is {}, expected 240", replay.framerate);
        }
        note(fmt::format("replay loaded: {} clicks", clicks));
    }
}

class $modify(SnapGameLayer, GJBaseGameLayer) {
    bool snapActive() {
        if (!g_triedLoad) {
            g_triedLoad = true;
            ensureLoaded();
        }
        return g_replayOk && enabled() && windowFrames() > 0
            && this == static_cast<GJBaseGameLayer*>(PlayLayer::get());
    }

    int64_t snapFrame() {
        return static_cast<int64_t>(m_gameState.m_currentProgress);
    }

    // Every real click/release goes through here.
    void handleButton(bool down, int button, bool isPlayer1) {
        if (!this->snapActive()) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        int64_t now = this->snapFrame();
        auto d = g_snap.onInput(down, button, !isPlayer1, now, windowFrames(), frameOffset());

        if (down) {
            if (d.snapped) {
                note(fmt::format("click snapped: {} frames early (frame {} -> {})", d.delay, now, d.target));
            } else {
                note(fmt::format("click not corrected (frame {})", now));
            }
        }

        // delayed inputs are held back and sent later from processCommands
        if (d.passNow) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }

    // Runs every physics step: send any held-back clicks whose frame has arrived.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick && this->snapActive()) {
            int64_t now = this->snapFrame();
            if (now < g_lastFrame) g_snap.reset(); // level restarted or checkpoint loaded
            g_lastFrame = now;

            std::vector<snap::Pending> due;
            g_snap.popDue(now, due);
            for (auto const& e : due) {
                note(fmt::format("{} sent at frame {}", e.down ? "click" : "release", now));
                GJBaseGameLayer::handleButton(e.down, e.button, !e.player2);
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(SnapPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        ensureLoaded();   // picks up a new/changed replay file
        g_snap.reset();   // drop anything still queued from before the restart
        g_lastFrame = 0;
    }
};
