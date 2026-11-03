// Noclip Death Sound — addon for Mega Hack v9 (Geometry Dash 2.2081, Geode).
//
// How it works:
//   Mega Hack's noclip lets PlayLayer::destroyPlayer be called but prevents the
//   actual death. We hook destroyPlayer with the highest priority (we run BEFORE
//   Mega Hack), call the original chain, and check afterwards: if the player
//   touched a hazard but is still alive, that is a "noclip death" -> play a sound.
//   Contact lasts many physics ticks, so only the first tick of each contact
//   triggers the sound.
//
// Custom sound: drag any audio file (mp3, ogg, wav, flac...) onto the game
// window. It is copied into the mod's config folder and used as the death sound.

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>

#include <mh/mh.hpp>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Settings (stored in Geode settings; the Mega Hack tab mirrors them)
// ---------------------------------------------------------------------------
namespace cfg {
    static bool enabled()          { return Mod::get()->getSettingValue<bool>("enabled"); }
    static double volume()         { return Mod::get()->getSettingValue<double>("volume"); }
    static bool useSfxVolume()     { return Mod::get()->getSettingValue<bool>("respect-sfx-volume"); }
    static double cooldown()       { return Mod::get()->getSettingValue<double>("cooldown"); }
    static double contactGap()     { return Mod::get()->getSettingValue<double>("contact-gap"); }
    static bool player1()          { return Mod::get()->getSettingValue<bool>("player-1"); }
    static bool player2()          { return Mod::get()->getSettingValue<bool>("player-2"); }
    static bool dragDrop()         { return Mod::get()->getSettingValue<bool>("drag-drop"); }
    static std::filesystem::path soundFile() {
        return Mod::get()->getSettingValue<std::filesystem::path>("sound-file");
    }
    static void setSoundFile(std::filesystem::path const& p) {
        Mod::get()->setSettingValue<std::filesystem::path>("sound-file", p);
    }
}

// UTF-8 string (Mega Hack textbox) -> path. std::filesystem::path(std::string)
// would use the ANSI code page on Windows and break Cyrillic paths.
static std::filesystem::path pathFromUtf8(std::string const& s) {
    return std::filesystem::path(utils::string::utf8ToWide(s));
}

// ---------------------------------------------------------------------------
// Sound playback (FMOD directly: any file path, own volume)
// ---------------------------------------------------------------------------
namespace sound {
    static FMOD::Sound* g_sound = nullptr;
    static std::string g_loadedPath;

    static std::string resolvePath() {
        std::error_code ec;
        auto custom = cfg::soundFile();
        if (!custom.empty() && std::filesystem::is_regular_file(custom, ec)) {
            return utils::string::pathToString(custom);
        }
        // Fallback: the standard GD death sound
        return CCFileUtils::get()->fullPathForFilename("explode_11.ogg", false);
    }

    static void release() {
        if (g_sound) {
            g_sound->release();
            g_sound = nullptr;
        }
        g_loadedPath.clear();
    }

    // Checks that FMOD can decode the file (used before accepting a dropped file)
    static bool canDecode(std::filesystem::path const& file) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return false;
        FMOD::Sound* test = nullptr;
        auto res = engine->m_system->createSound(
            utils::string::pathToString(file).c_str(), FMOD_CREATESTREAM, nullptr, &test);
        if (test) test->release();
        return res == FMOD_OK;
    }

    static void play() {
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return;

        auto path = resolvePath();
        if (path.empty()) return;

        if (!g_sound || path != g_loadedPath) {
            release();
            auto res = engine->m_system->createSound(path.c_str(), FMOD_DEFAULT, nullptr, &g_sound);
            if (res != FMOD_OK || !g_sound) {
                log::error("Failed to load death sound '{}' (FMOD error {})", path, static_cast<int>(res));
                g_sound = nullptr;
                return;
            }
            g_loadedPath = path;
            log::info("Loaded death sound: {}", path);
        }

        float vol = static_cast<float>(cfg::volume());
        if (cfg::useSfxVolume()) vol *= engine->m_sfxVolume;
        if (vol <= 0.f) return;

        FMOD::Channel* channel = nullptr;
        if (engine->m_system->playSound(g_sound, nullptr, true, &channel) == FMOD_OK && channel) {
            channel->setVolume(vol);
            channel->setPaused(false);
        }
    }
}

// ---------------------------------------------------------------------------
// Drag & drop an audio file onto the game window
// ---------------------------------------------------------------------------
namespace drop {
    static HWND g_hwnd = nullptr;
    static WNDPROC g_prevProc = nullptr;

    static bool isAudioExt(std::filesystem::path const& p) {
        auto ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return std::towlower(c); });
        static constexpr wchar_t const* known[] = {
            L".mp3", L".ogg", L".oga", L".wav", L".flac", L".aif", L".aiff",
            L".wma", L".mp2", L".opus", L".m4a", L".aac", L".mod", L".xm", L".it", L".s3m",
        };
        for (auto k : known) if (ext == k) return true;
        return false;
    }

    static void notify(std::string const& text, NotificationIcon icon) {
        Notification::create(text, icon, 2.5f)->show();
    }

    // Copies the file into the config folder (so it keeps working if the
    // original is moved or deleted) and makes it the death sound.
    static void applyFile(std::filesystem::path const& src) {
        std::error_code ec;
        sound::release();
        auto dir = Mod::get()->getConfigDir();
        // remove the previous custom sound(s)
        for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().stem() == L"custom_death_sound") std::filesystem::remove(entry.path(), ec);
        }

        auto ext = src.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return std::towlower(c); });
        auto dst = dir / (L"custom_death_sound" + ext);

        ec.clear();
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            log::error("Failed to copy '{}': {}", utils::string::pathToString(src), ec.message());
            notify("Could not copy the sound file", NotificationIcon::Error);
            return;
        }
        if (!sound::canDecode(dst)) {
            std::filesystem::remove(dst, ec);
            notify("This audio format is not supported", NotificationIcon::Error);
            return;
        }

        cfg::setSoundFile(dst);
        sound::release();
        log::info("Custom death sound set from '{}'", utils::string::pathToString(src));
        notify("Death sound: " + utils::string::pathToString(src.filename()), NotificationIcon::Success);
        sound::play(); // preview
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_DROPFILES && cfg::dragDrop()) {
            auto hdrop = reinterpret_cast<HDROP>(wp);
            UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(hdrop, i, nullptr, 0);
                std::wstring buf(len + 1, L'\0');
                DragQueryFileW(hdrop, i, buf.data(), len + 1);
                buf.resize(len);
                std::filesystem::path file(buf);
                if (isAudioExt(file)) {
                    // Audio file: it's ours. Take the first one and finish the drop.
                    DragFinish(hdrop);
                    applyFile(file);
                    return 0;
                }
            }
            // Not audio: let the game / other mods handle it
        }
        return CallWindowProcW(g_prevProc, hwnd, msg, wp, lp);
    }

    static BOOL CALLBACK findWindow(HWND hwnd, LPARAM out) {
        wchar_t cls[64] = {};
        GetClassNameW(hwnd, cls, 64);
        if (std::wstring_view(cls).starts_with(L"GLFW") && IsWindowVisible(hwnd)) {
            *reinterpret_cast<HWND*>(out) = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    // Called on the main thread; re-installs if the game recreated its window.
    static void install() {
        HWND hwnd = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), findWindow, reinterpret_cast<LPARAM>(&hwnd));
        if (!hwnd || hwnd == g_hwnd) return;

        DragAcceptFiles(hwnd, TRUE);
        auto prev = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndProc)));
        if (!prev) {
            log::error("Drag & drop: failed to hook the game window ({})", GetLastError());
            return;
        }
        g_prevProc = prev;
        g_hwnd = hwnd;
        log::info("Drag & drop of death sounds enabled");
    }
}

class $modify(NoclipDeathSoundML, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        queueInMainThread([] { drop::install(); });
        return true;
    }
};

// ---------------------------------------------------------------------------
// Noclip "death" detection
// ---------------------------------------------------------------------------
class $modify(NoclipDeathSoundPL, PlayLayer) {
    struct Fields {
        double lastContact[2] = {-1.0, -1.0}; // level time of the last hazard contact, per player
        double lastSound = -1.0;              // level time of the last played sound
    };

    static void onModify(auto& self) {
        // Run before Mega Hack's destroyPlayer detour, so our hook wraps it.
        if (auto res = self.setHookPriority("PlayLayer::destroyPlayer", Priority::First); !res) {
            log::warn("Could not set destroyPlayer hook priority: {}", res.unwrapErr());
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // The anti-cheat spike is not a real collision; an already dead player is not interesting.
        bool candidate = player && object != m_anticheatSpike && !player->m_isDead && !m_playerDied
                      && !m_levelEndAnimationStarted;

        PlayLayer::destroyPlayer(player, object);

        if (!candidate || !cfg::enabled()) return;

        // Real death -> GD plays its own sound, nothing to do.
        if (player->m_isDead || m_playerDied) return;

        int idx = (player == m_player2) ? 1 : 0;
        if (idx == 0 && !cfg::player1()) return;
        if (idx == 1 && !cfg::player2()) return;

        auto f = m_fields.self();
        double now = m_gameState.m_levelTime;
        double prev = f->lastContact[idx];
        f->lastContact[idx] = now;

        // Still the same contact (hazard touched on consecutive ticks)?
        bool sameContact = prev >= 0.0 && now >= prev && (now - prev) <= cfg::contactGap();
        if (sameContact) return;

        // Cooldown between sounds
        if (f->lastSound >= 0.0 && now >= f->lastSound && (now - f->lastSound) < cfg::cooldown()) return;

        f->lastSound = now;
        sound::play();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        f->lastContact[0] = f->lastContact[1] = -1.0;
        f->lastSound = -1.0;
    }
};

// ---------------------------------------------------------------------------
// Mega Hack tab
// ---------------------------------------------------------------------------
static void setupMegaHackTab() {
    static mh::Tab tab("NOCLIP_DEATH_SOUND", "Noclip Death Sound");
    static bool syncing = false;

    auto mod = Mod::get();

    // Write a value from the MH tab into Geode settings
    auto toSetting = [mod](char const* key) {
        return [mod, key](auto value) {
            if (syncing) return;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) mod->setSettingValue<bool>(key, value);
            else mod->setSettingValue<double>(key, static_cast<double>(value));
        };
    };

    tab.persist(false) // values live in Geode settings
        .checkbox("ENABLED", "Death Sound", cfg::enabled(), toSetting("enabled"))
        .spinner("VOLUME", "Volume", cfg::volume(), {.min = 0.0, .max = 2.0, .step = 0.05}, toSetting("volume"))
        .checkbox("SFX_VOLUME", "Use Game SFX Volume", cfg::useSfxVolume(), toSetting("respect-sfx-volume"))
        .spinner("COOLDOWN", "Cooldown (sec)", cfg::cooldown(), {.min = 0.0, .max = 5.0, .step = 0.05}, toSetting("cooldown"))
        .checkbox("PLAYER_1", "Player 1", cfg::player1(), toSetting("player-1"))
        .checkbox("PLAYER_2", "Player 2", cfg::player2(), toSetting("player-2"))
        .textbox("SOUND_PATH", "Sound File (or drag a file into the game)",
                 utils::string::pathToString(cfg::soundFile()),
                 [mod](std::string const& value) {
                     if (syncing) return;
                     mod->setSettingValue<std::filesystem::path>("sound-file", pathFromUtf8(value));
                     sound::release();
                 })
        .button("TEST", "Test Sound", [] { sound::play(); })
        .button("RESET_SOUND", "Reset to Default Sound", [] {
            cfg::setSoundFile({});
            sound::release();
            tab.set("SOUND_PATH", std::string());
            drop::notify("Death sound: default", NotificationIcon::Info);
        })
        .button("SETTINGS", "Open Geode Settings (file picker)", [] { openSettingsPopup(Mod::get()); })
        // When the menu opens, pull the current Geode settings into the tab
        .onOpen([] {
            syncing = true;
            tab.set("ENABLED", cfg::enabled());
            tab.set("VOLUME", cfg::volume());
            tab.set("SFX_VOLUME", cfg::useSfxVolume());
            tab.set("COOLDOWN", cfg::cooldown());
            tab.set("PLAYER_1", cfg::player1());
            tab.set("PLAYER_2", cfg::player2());
            tab.set("SOUND_PATH", utils::string::pathToString(cfg::soundFile()));
            syncing = false;
        })
        // When the menu closes, push the tab values into Geode settings
        .onCommit([mod] {
            mod->setSettingValue<bool>("enabled", tab.get<bool>("ENABLED"));
            mod->setSettingValue<double>("volume", tab.get<double>("VOLUME"));
            mod->setSettingValue<bool>("respect-sfx-volume", tab.get<bool>("SFX_VOLUME"));
            mod->setSettingValue<double>("cooldown", tab.get<double>("COOLDOWN"));
            mod->setSettingValue<bool>("player-1", tab.get<bool>("PLAYER_1"));
            mod->setSettingValue<bool>("player-2", tab.get<bool>("PLAYER_2"));
            auto path = tab.get<std::string>("SOUND_PATH");
            if (path != utils::string::pathToString(cfg::soundFile())) {
                mod->setSettingValue<std::filesystem::path>("sound-file", pathFromUtf8(path));
                sound::release();
            }
        });

    if (!tab.registerTab()) {
        log::error("Noclip Death Sound: failed to register Mega Hack tab: {}", tab.lastError());
    } else {
        log::info("Noclip Death Sound: Mega Hack tab registered");
    }
}

$on_mod(Loaded) {
    // Reload the sound when the file setting changes in the Geode UI
    listenForSettingChanges<std::filesystem::path>("sound-file", [](std::filesystem::path) { sound::release(); });

    // Mega Hack loads later than us: register the tab once the main menu is shown.
    // Without Mega Hack the mod still works (any noclip), settings are in the Geode menu.
    mh::onReady(setupMegaHackTab, 5);
}
