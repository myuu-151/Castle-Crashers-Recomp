#include "player/game.h"

#include <SDL3/SDL_log.h>

#include "as/builtins.h"
#include "as/rng.h"
#include "audio/audio.h"
#include "common/files.h"
#include "menu/pause_menu.h"
#include "player/mods.h"
#include "render/renderer.h"

namespace player {

namespace {

constexpr float kTick = 1.0f / 30.0f;

// Flash globals the engine itself reads and writes.
constexpr as::NameId kNoPause = 0xdb;         // g_bNoPause
constexpr as::NameId kLoading = 0xe1;         // g_bLoading
constexpr as::NameId kReadyToLoad = 0xe2;     // g_bReadyToLoad
constexpr as::NameId kNoLoadingScreen = 0xe3; // g_bNoLoadingScreen
constexpr as::NameId kPDLC = 0xd6;           // g_nPDLC
constexpr as::NameId kPort1State = 0xc7;     // g_nPort1State..g_nPort4State
constexpr as::NameId kPort1Id = 0xcb;        // g_nPort1..: set when a game starts
constexpr as::NameId kLoadIcon = 0xe0;       // g_bLoadIcon
constexpr as::NameId kQuit = 0xdd;           // the level's "quit" clip, and its label
constexpr as::NameId kQuitTo = 0xde;         // its "quitto" variable

}  // namespace

Game::Game(std::filesystem::path swf_dir) : swf_dir_(std::move(swf_dir)) {}

std::string Game::file_to_name(const std::string& file) const {
    // "legal.cok6" -> "legal"
    auto dot = file.find('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

std::unique_ptr<Player> Game::load(const std::string& name) {
    for (const char* folder : {"game", "levels"}) {
        auto path = swf_dir_ / folder / (name + ".swf");
        if (!files::exists(path.string())) continue;
        auto movie = swf::Movie::load(path.string());
        if (!movie) break;
        if (!mod_dir.empty()) apply_mod(*movie, mod_dir);
        return std::make_unique<Player>(*this, std::move(movie), name);
    }
    SDL_Log("can't load movie %s", name.c_str());
    return nullptr;
}

void Game::start(const std::string& movie) {
    // Signing in loads the save. The 2012 build reads
    // cc_save.dat from Steam Cloud; a missing or newer save fails its
    // checksum and a fresh one is made, which is what this does. The DLC
    // mask goes to g_nPDLC and unlocks its characters.
    storage.reset(dlc);
    // A live run keeps its progress in a save file, or the port's storage.
    if (read_save_data) {
        std::vector<uint8_t> bytes;
        if (read_save_data(bytes) && bytes.size() >= save::Storage::kSize) {
            bytes.resize(save::Storage::kSize);
            saved = bytes;
            storage.assign(bytes);
            storage.apply_dlc(dlc);
            storage.sanitize();
        }
    } else if (!save_file.empty()) {
        if (FILE* f = std::fopen(save_file.string().c_str(), "rb")) {
            std::vector<uint8_t> bytes(save::Storage::kSize);
            if (std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size()) {
                saved = bytes;
                storage.assign(bytes);
                storage.apply_dlc(dlc);
                storage.sanitize();
            }
            std::fclose(f);
        }
    }
    if (max_character > 0) storage.max_out(max_character);   // (testing)
    set_flash_global(kPDLC, int(dlc));
    if (!movie.empty()) {
        as::rng().reseed();
        current_ = load(movie);
        state_ = 2;
        return;
    }
    state_ = 0;
    boot_ticks_ = 0;
}

void Game::tick() {
    if (state_ == 7) return;  // quitting
    if (suspend_request >= 0) {
        suspended = suspend_request != 0;
        suspend_request = -1;
    }
    // Input is sampled first (the original's step 2); the ports' FlashPads run
    // once the boot has set them up.
    input.focused = window_active;
    audio::manager().set_focus(window_active);
    if (state_ != 0) input.tick(dt, sample_input_);
    // Each sample taken publishes its port's state.
    if (state_ != 0 && sample_input_)
        for (int i = 0; i < 4; i++) set_flash_global(as::NameId(kPort1State + i), input.devices[i].port_state & 0xf);
    released_.reset();
    unloaded_.clear();
    // Nodes no display list reaches go now, while nothing is mid-update.
    for (Player* p : {current_.get(), loading_.get(), pause_.get(), incoming_.get()})
        if (p) p->sweep_nodes();
    for (auto& s : subs_)
        if (s.player) s.player->sweep_nodes();
    if (state_ == 0) {  // boot: wait 30 ticks, then logo; loading and pause stay resident
        if (++boot_ticks_ < 30) return;
        as::rng().reseed();
        current_ = load("logo");
        // The update that finds the movie loaded only marks it ready (the
        // real game's capture shows it on frame 0); the next one plays.
        if (current_) current_->ready_update();
        if (current_) current_->update(dt);
        // Loaded resident, each marked ready (the capture shows one update
        // at frame 0) and then left inactive.
        loading_ = load("loading");
        if (loading_) loading_->ready_update();
        pause_ = load("pause");
        if (pause_) pause_->ready_update();
        // The pause FlashController, made once.
        if (pause_) pause_menu_ = std::make_unique<menu::PauseMenu>(*this, *pause_);
        state_ = 2;
        check_pause();
        return;
    }
    check_pause();
    // The movies: while a sub-movie loads (state 4) only the loading screen
    // runs; otherwise the FlashController, then the current movie and the
    // loading screen if it is up.
    // The active FlashController, then the movies in the scene's order:
    // the current one, the pause movie while its node is active, the
    // loading screen.
    if (state_ != 4) {
        if (menu::BaseMenu* c = active_controller()) c->update(dt);
        if (current_) current_->update(dt);
        if (pause_ && pause_shown_) pause_->update(dt);
    }
    if (loading_ && loading_active_) loading_->update(dt);
    if (!suspended) switch (state_) {
    case 3:  // waiting for the loading screen to cover the screen
        sample_input_ = false;
        input_off_ = 1;
        if (get_flash_global(kReadyToLoad) == 1 || get_flash_global(kNoLoadingScreen) != 0) {
            current_.reset();  // the old movie is destroyed
            subs_.clear();
            state_ = 1;
            incoming_updates_ = 0;
        }
        break;
    case 1: {  // loading the new movie
        if (!incoming_) {
            as::rng().reseed();
            incoming_ = load(file_to_name(pending_));
            if (!incoming_) {
                state_ = 2;
                break;
            }
            incoming_->ready_update();
        }
        // Once ready it gets 3 updates, not displayed.
        if (incoming_updates_ < 3) {
            incoming_->update(dt);
            if (++incoming_updates_ == 3) set_flash_global(kLoading, 0);
            break;
        }
        bool hidden = get_flash_global(kReadyToLoad) == 0 || get_flash_global(kNoLoadingScreen) == 1 || !loading_;
        if (!hidden) {
            incoming_->update(dt);
            break;
        }
        current_ = std::move(incoming_);
        state_ = 2;
        set_flash_global(kNoLoadingScreen, 0);
        input_off_ = 0;
        sample_input_ = true;
        break;
    }
    case 4:  // loading a sub-movie
        tick_sub_movie();
        break;
    default:
        break;
    }
    // The main loop's other iteration of the tick (doc 2.1) checks again,
    // with the same pad states.
    check_pause();
}

// ---- Pause

void Game::check_pause() {
    // the original's, offline: no Steam overlay, invites or remote players.
    if (state_ == 0) return;  // the pads are set up at boot (ctxs+0xb04)
    auto count = [](uint32_t mask) {
        int n = 0;
        for (int i = 0; i < 4; i++) n += (mask >> i) & 1;
        return n;
    };
    if (pause_flags_ & 0x100) {
        // The window was lost while the pause wasn't allowed.
        if (!active_controller() && get_flash_global(kNoPause) != 1 && count(registered_ports) != 0)
            open_pause(-3, 1);
        if (window_active) pause_flags_ &= ~0x100u;
        return;
    }
    if (!window_active) {
        if (get_flash_global(kNoPause) == 1) {
            pause_flags_ |= 0x100;
            return;
        }
        if (!active_controller() && count(registered_ports) != 0) open_pause(-3, 0x101);
        return;
    }
    int disconnected = 0;
    if (state_ != 1 && state_ != 4 && get_flash_global(kLoadIcon) == 0) {
        int nopause = get_flash_global(kNoPause);
        for (int port = 0; port < 4; port++) {
            if (!(registered_ports & (1u << port))) continue;
            // the port's device is connected (the keyboard
            // is part of the first).
            bool connected = port == 0 || input.pads[port].connected;
            if (!connected) {
                if (nopause != 1) {
                    disconnected++;
                    open_pause(port, 5);
                }
            } else if (nopause == 0 && input.ports[port].buttons[input::FlashPad::kStartButton].pressed()) {
                open_pause(port, 1);  // Start pressed
            }
        }
    }
    if ((pause_flags_ & 4) && disconnected == 0) close_pause(-1, 4);
}

void Game::open_pause(int port, uint32_t reason) {
    // the original's, open.
    if (pause_flags_ & 1) return;
    pause_flags_ |= reason & ~1u;
    if (!current_ || loading_sub_ >= 0) return;
    if (controller_) return;  // a movie's own menu is up
    input.keyboard.override_map("defaultmenu");
    cursor_shown = true;  // the original's(1)
    pause_flags_ |= reason;
    // the port's state becomes 2 (paused), and every playing
    // registered port's.
    auto pause_playing = [&] {
        for (int i = 0; i < 4; i++) {
            uint8_t& s = input.devices[i].port_state;
            if ((s & 0xf) == 1 && (registered_ports & (1u << i))) s = uint8_t((s & 0xf2) | 2);
        }
    };
    if (port >= 0 && port < 4) {
        paused_ports_ |= 1u << port;
        uint8_t& s = input.devices[port].port_state;
        s = uint8_t((s & 0xf2) | 2);
        pause_playing();
    }
    if (port == -3) pause_playing();
    // the pause controller becomes the active one, and the
    // pause movie's node is updated and drawn from now on.
    pause_shown_ = true;
    if (pause_menu_) static_cast<menu::PauseMenu&>(*pause_menu_).opened(reason, port);
    current_->paused = true;
    audio::manager().pause_menu(true);  // the music and looping sounds muted
}

void Game::close_pause(int port, uint32_t reason) {
    // the original's, close.
    (void)port;
    if (!current_ || loading_sub_ >= 0) return;
    pause_flags_ &= ~reason;
    if (input.keyboard.overridden()) {
        input.keyboard.override_map(nullptr);
        cursor_shown = false;  // the original's(0)
    }
    if (pause_flags_ == 1 && !pause_shown_) pause_flags_ = 0;
    if (pause_flags_ != 0) return;
    if (pause_shown_) {
        if (pause_menu_ && pause_menu_->current) {
            menu::Page& p = *pause_menu_->current;
            p.fade = 1;
            p.input_on = false;
            p.fade_alpha = 0.0f;
            if (p.port < 0) p.port = p.input_port = -2;
        }
        pause_shown_ = false;
    }
    current_->paused = false;
    audio::manager().pause_menu(false);
    // paused ports play again.
    for (auto& d : input.devices)
        if ((d.port_state & 0xf) == 2) d.port_state = uint8_t((d.port_state & 0xf1) | 1);
    paused_ports_ = 0;
}

void Game::mouse_event(int message, float x, float y) {
    if (menu::BaseMenu* c = active_controller()) c->pointer(message, x, y);
}

void Game::register_port(int port) {
    // the original's, offline.
    if (port < 0 || port > 3 || (registered_ports & (1u << port))) return;
    uint8_t& s = input.devices[port].port_state;
    s = uint8_t((s & 0xf1) | 1);
    registered_ports |= 1u << port;
}

void Game::start_local_game(int port, int mode) {
    // the original's (offline): the menu's settings are kept (the mode), the
    // port registered and made the host; the original's opens the session.
    game_mode = mode;
    last_game_mode = mode;
    host_port = port;
    local_game = true;
    in_session = true;
    register_port(port);
    // The level reads the port's state before any sample publishes it
    // (sampling stops with the movie change in the same tick): the captures
    // show it 1 from the start, so it is set here too.
    if (port >= 0 && port < 4) set_flash_global(as::NameId(kPort1State + port), 1);
}

void Game::unregister_port(int port) {
    // the original's, offline.
    if (!registered(port)) return;
    input.devices[port].port_state &= 0xf0;
    registered_ports &= ~(1u << port);
    if (!local_game || registered_ports != 0) {
        if (host_port == port) {
            for (int i = 0; i < 4; i++)
                if (registered(i)) {
                    host_port = i;
                    break;
                }
        }
    } else {
        reset_game();
    }
}

void Game::leave_game() {
    // the original's offline.
    if (in_session) write_all_stats();
    reset_game();
}

void Game::reset_game() {
    // the original's.
    game_mode = -1;
    in_session = false;
    local_game = false;
    registered_ports = 0;
    for (int i = 0; i < 4; i++) {
        set_flash_global(as::NameId(kPort1Id + i), 0);
        set_flash_global(as::NameId(kPort1State + i), 0);
    }
}

void Game::write_all_stats() {
    // the original's -> the original's (offline only arcade) -> // the character with the most XP among the highest levels, read
    // record by record (the leaderboard upload itself is Steam's). The
    // shared storage cursor is left after the last read.
    if (!registered(host_port) || game_mode != 0) return;
    uint32_t best_level = 1, best_xp = 0;
    for (uint32_t i = 0; i < 30; i++) {
        uint32_t level = storage.read_be(0x41 + i * 0x30, 1) + 1;
        if (level > 99) level = 0;
        if (best_level <= level) {
            uint32_t xp = 0;
            for (int k = 0; k < 4; k++) {
                uint8_t b = 0;
                storage.read(b);
                xp = (xp << 8) | b;
            }
            if (best_xp < xp) {
                best_level = level;
                best_xp = xp;
            }
        }
    }
}

void Game::show_load_icon(bool on) {
    set_flash_global(kLoadIcon, on ? 1 : 0);
    // HideLoadIcon: the loading node's update (the original's(1.0)), run
    // inside the script call, when the node is active.
    if (!on && loading_ && loading_active_) loading_->update(1.0f);
}

bool Game::write_save(int port) {
    // WriteSaveGame (0x447008): ports past 3 do nothing; a
    // port outside a running local game's players doesn't write. Every
    // write succeeds here, so the answer is always true.
    if (port < 0 || port > 3) return true;
    if (local_game && !registered(port)) return true;
    saved = storage.bytes();
    if (write_save_data) {
        write_save_data(saved);
    } else if (!save_file.empty()) {
        if (FILE* f = std::fopen(save_file.string().c_str(), "wb")) {
            std::fwrite(saved.data(), 1, saved.size(), f);
            std::fclose(f);
        }
    }
    return true;
}

void Game::read_save(int port) {
    // ReadSaveGame: the save last written, else a fresh one;
    // the DLC re-applied. (Its settings part, volumes and gore, comes with
    // the audio.)
    if (port < 0 || port > 3) return;
    set_flash_global(kPDLC, int(dlc));
    if (saved.size() >= save::Storage::kSize) {
        storage.assign(saved);
        storage.apply_dlc(dlc);
        storage.sanitize();
    } else {
        storage.reset(dlc);
    }
    if (max_character > 0) storage.max_out(max_character);   // (testing)
}

void Game::tick_sub_movie() {
    if (loading_sub_ < 0 || loading_sub_ >= int(subs_.size())) {
        state_ = saved_state_;
        return;
    }
    SubMovie& s = subs_[size_t(loading_sub_)];
    if (!s.player) {
        // Loaded: this update makes it ready, and does nothing else.
        s.player = load(as::names().str(s.name));
        if (s.player) {
            s.player->sub_movie = true;
            s.player->ready_update();
        }
        return;
    }
    // Ready: the next one waiting, or back to what was running.
    for (size_t i = 0; i < subs_.size(); i++) {
        if (subs_[i].used && !subs_[i].player) {
            loading_sub_ = int(i);
            return;
        }
    }
    if (input_off_ == 2) {
        input_off_ = 0;
        sample_input_ = true;
    }
    loading_sub_ = -1;
    state_ = saved_state_;
}

void Game::render(render::Renderer& r) {
    if (current_) current_->render(r);
    if (pause_ && pause_shown_) pause_->render(r);
    if (menu::BaseMenu* c = active_controller()) c->render(r);
    if (loading_ && loading_active_ && state_ != 0) loading_->render(r);
}

Player* Game::sub_movie(as::NameId name) {
    for (SubMovie& s : subs_) {
        if (s.used && s.name == name) return s.player.get();
    }
    return nullptr;
}

std::string Game::loading_state() const {
    const size_t queued = current_ ? current_->load_queue.size() : 0;
    const int loading = get_flash_global(kLoading);
    if (state_ == 2 && loading_sub_ < 0 && queued == 0 && loading == 0) return {};
    std::string out = "state " + std::to_string(state_) + " (then " + std::to_string(saved_state_) + ")";
    if (!pending_.empty()) out += ", changing to " + pending_;
    if (incoming_) out += ", incoming after " + std::to_string(incoming_updates_) + " updates";
    out += ", flags loading " + std::to_string(loading) + " ready " + std::to_string(get_flash_global(kReadyToLoad)) +
           " no screen " + std::to_string(get_flash_global(kNoLoadingScreen));
    out += "; sub-movies";
    for (size_t i = 0; i < subs_.size(); i++) {
        const SubMovie& s = subs_[i];
        if (!s.used) continue;
        out += " " + std::string(as::names().str(s.name)) + (s.player ? " ready" : " loading") + " x" +
               std::to_string(s.refs) + (int(i) == loading_sub_ ? " (now)" : "");
    }
    out += "; queued " + std::to_string(queued);
    for (size_t i = 0; i < queued && i < 3; i++) {
        const Player::LoadEntry& e = current_->load_queue[i];
        bool alive = e.target->refcount != 0 && e.target->serial == e.serial;
        out += " " + std::string(as::names().str(e.name)) + (alive ? "" : " (target gone)");
    }
    return out;
}

std::string Game::input_state() const {
    std::string out = "state " + std::to_string(state_) + ", input " + (sample_input_ ? "on" : "off");
    if (input_off_) out += " (off for " + std::string(input_off_ == 1 ? "a movie change" : "a loadMovie") + ")";
    char flags[64];
    std::snprintf(flags, sizeof(flags), ", pause flags %x%s", unsigned(pause_flags_), pause_shown_ ? " (shown)" : "");
    out += flags;
    out += controller_ ? ", a movie's menu is up" : "";
    out += loading_active_ ? ", loading screen on" : "";
    out += ", no pause " + std::to_string(get_flash_global(kNoPause)) + ", ports " + std::to_string(registered_ports);
    return out;
}

void Game::change_movie(const std::string& file) {
    if (state_ != 2) return;
    if (get_flash_global(kNoLoadingScreen) == 0) {
        set_flash_global(kReadyToLoad, 0);
        set_flash_global(kLoading, 1);
        loading_active_ = true;  // the loading movie is activated
    }
    released_ = std::move(controller_);  // the controller is released
    set_flash_global(kNoPause, 1);
    pending_ = file;
    state_ = 3;
}

void Game::quit_to(const std::string& file) {
    // QuitTo: the level plays its "quit" clip, which fades out
    // and calls ChangeMovie(quitto) itself; a movie without one changes at
    // once. The pause closes whatever its reason.
    if (!current_ || get_flash_global(kLoading) != 0 || state_ == 1 || state_ == 4) {
        SDL_Log("QuitTo(%s) while loading: to be replayed after the load (gm+0x47), not ported", file.c_str());
        return;
    }
    as::NameId name = as::names().intern(file);
    MovieClip* quit = nullptr;
    for (Character* c = current_->root()->first_child; c; c = c->next)
        if (c->name == kQuit) {
            quit = MovieClip::from(c);
            break;
        }
    audio::manager().stop_all_loops();
    close_pause(-1, 0x11b);
    if (!quit) {
        change_movie(file);
        return;
    }
    as::Value v;
    v.set_string(name);
    quit->props.get_or_add(kQuitTo).store(v);
    // gotoAndPlay("quit").
    uint16_t f = 0;
    for (const auto& [label, frame] : quit->labels)
        if (label == kQuit) {
            f = frame;
            break;
        }
    if (f == 0) return;
    if (quit->total_frames < f) f = quit->total_frames;
    if (f == quit->frame) quit->flags |= MovieClip::kPlaying;
    quit->flags &= uint8_t(~MovieClip::kStopRequested);
    quit->goto_frame(f);
}

void Game::set_flash_controller(const std::string& name, Player& movie) {
    released_ = std::move(controller_);
    controller_ = menu::create(name, *this, movie);
}

int Game::get_flash_global(as::NameId name) const {
    auto it = globals_.find(name);
    return it == globals_.end() ? 0 : it->second;
}

void Game::set_flash_global(as::NameId name, int value) { globals_[name] = value; }

bool Game::key_down(int code) {
    if (replaying) {
        if (replay.empty()) return false;
        auto [recorded, result] = replay.front();
        replay.pop_front();
        if (recorded != code && replay_mismatches++ < 10)
            SDL_Log("replay: asked Key.isDown(%d) where the real game asked %d", code, recorded);
        return recorded == code && result;
    }
    return input.key_down(code);
}

void Game::load_movie(MovieClip* target, as::NameId name) {
    // queued on the target's context movie (the host, for a
    // clip of a loaded movie), which clones the loaded movie into it once
    // it is ready.
    if (!target->player) return;  // a clip whose movie went (~Player)
    if (boot_level > 0 && !boot_level_done) {
        // TESTING (boot_level): the first level loaded is that one instead (a
        // level's movie arrives here as "levelN": the interpreter's GetURL2
        // takes "../levelN/levelN.swf" down to its name)
        const std::string& movie = as::names().str(name);
        if (movie.size() > 5 && movie.compare(0, 5, "level") == 0 &&
            movie.find_first_not_of("0123456789", 5) == std::string::npos) {
            boot_level_done = true;
            const std::string to = "level" + std::to_string(boot_level);
            SDL_Log("boot level: %s -> %s", movie.c_str(), to.c_str());
            name = as::names().intern(to);
        }
    }
    target->player->context().load_queue.push_back({target, target->serial, name});
    // GameManager::LoadMovie: one slot per name, counted.
    for (SubMovie& s : subs_) {
        if (s.used && s.name == name) {
            s.refs++;
            return;
        }
    }
    if (state_ != 4) {
        saved_state_ = state_;
        state_ = 4;
    }
    // The first free slot.
    size_t slot = 0;
    while (slot < subs_.size() && subs_[slot].used) slot++;
    if (slot == subs_.size()) subs_.emplace_back();
    subs_[slot] = {name, nullptr, 1, true};
    if (loading_sub_ < 0) {
        loading_sub_ = int(slot);
        if (input_off_ == 0) {
            input_off_ = 2;
            sample_input_ = false;
        }
    }
}

void Game::stop_all_sounds() { audio::manager().stop_all_loops(); }

void Game::release_loaded_movie(Player* movie) { unload_sub_movie(movie); }

void Game::unload_sub_movie(Player* movie) {
    // the last reference destroys the movie and frees its
    // slot (the name is cleared, 0x438223), so loading it again loads it
    // from the file again.
    for (SubMovie& s : subs_) {
        if (!s.used || s.player.get() != movie || s.refs <= 0) continue;
        if (--s.refs == 0) {
            unloaded_.push_back(std::move(s.player));
            s.used = false;
        }
        return;
    }
}

void Game::unload_movie(MovieClip* target) {
    // the clip is emptied in place.
    target->unload();
    if (target->flags & MovieClip::kLoadedMovieRoot) {
        target->flags &= uint8_t(~MovieClip::kLoadedMovieRoot);
        unload_sub_movie(target->player);
        if (target->parent) target->player = target->parent->player;
    }
}

}  // namespace player
