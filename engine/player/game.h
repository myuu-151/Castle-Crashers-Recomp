// The game manager: castle.exe's GameManager (vtable 0x49afd0). It runs the
// fixed 30 Hz tick, owns the movies (the current one, plus the resident
// `loading` and `pause` movies), the Flash-global store that survives movie
// changes, and the movie-change state machine with its loading-screen
// handshake. See docs/engine/movieclips-and-frames.md 2.1, 3.
#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as/interpreter.h"
#include "input/input.h"
#include "menu/menu.h"
#include "player/bsp.h"
#include "player/player.h"
#include "save/storage.h"

namespace render { class Renderer; }

namespace player {

class Game : public as::Host {
public:
    explicit Game(std::filesystem::path swf_dir);

    // Boots like castle.exe (logo, then loading and pause resident), or
    // starts straight into `movie` if given.
    void start(const std::string& movie = "");
    // Called once per game tick (1/30 s).
    void tick();
    // The tick's length: 1/30 s, except that castle.exe hands a tick after a
    // slow frame the time measured (a replay sets it from "!! dt").
    float dt = 1.0f / 30.0f;
    void render(render::Renderer& r);

    Player* current() { return current_.get(); }
    // A movie on its own, not shown or updated by the game (castle --selftest).
    std::unique_ptr<Player> load_standalone(const std::string& name) { return load(name); }

    // Natives.
    void change_movie(const std::string& file);  // ChangeMovie("legal.cok6")
    void quit_to(const std::string& file);       // QuitTo
    // SetFlashController: the native menus ("menu", "results"), or none.
    void set_flash_controller(const std::string& name, Player& movie);
    int get_flash_global(as::NameId name) const;
    void set_flash_global(as::NameId name, int value);

    // The pause (docs/engine/pause.md). open_pause / close_pause are
    // `reason` bits in gm+0x48 (1 the pause menu, 4 a pad
    // disconnected, 0x100 the window lost focus...); the menu stays up until
    // no reason is left.
    void open_pause(int port, uint32_t reason);
    void close_pause(int port, uint32_t reason);
    // Every movie's frame counter stops while any reason is set (GameManager
    // vtable +0x10).
    bool frames_stopped() const { return pause_flags_ != 0; }
    // A local game's players (the original's offline): the port is registered
    // (online+0x5c) and its state becomes 1 (playing).
    void register_port(int port);
    // A local game starts: its mode kept, the port that
    // started it registered and made the host.
    void start_local_game(int port, int mode);
    // LeaveSession(port): one port leaves; the last one
    // leaving a running game ends it.
    void unregister_port(int port);
    // Leaving the game (the original's offline): the arcade stats are read for
    // the leaderboard, then the reset.
    void leave_game();
    // no ports, no game mode, every port's flash globals
    // cleared.
    void reset_game();
    // WriteAllStats for the host (the original's offline): only arcade, which
    // finds the best character for the leaderboard.
    void write_all_stats();
    // Show/HideLoadIcon: g_bLoadIcon; hiding it also updates the loading
    // screen once at once, if it is up.
    void show_load_icon(bool on);
    // WriteSaveGame / ReadSaveGame.
    bool write_save(int port);
    void read_save(int port);
    bool registered(int port) const { return port >= 0 && port < 4 && (registered_ports & (1u << port)); }
    uint32_t registered_ports = 0;  // online+0x5c
    int game_mode = -1;             // *online+0x20 (0 arcade, 1 quaff, 3 arena); -1 before any game
    int last_game_mode = 0;         // 0x658708, what GetGameMode answers without a game
    int host_port = 0;              // online+0x54
    bool local_game = false;        // online+0x7d5: a local game was started
    bool in_session = false;        // online+0x751
    // The save file (live runs; empty in captures and replays) and the bytes
    // last written, which ReadSaveGame goes back to.
    std::filesystem::path save_file;
    // Or a port's own save storage (a memory card): reading fills the bytes
    // and answers whether there was a save; writing keeps them.
    std::function<bool(std::vector<uint8_t>&)> read_save_data;
    std::function<void(const std::vector<uint8_t>&)> write_save_data;
    std::vector<uint8_t> saved;
    bool window_active = true;      // *(0x651ce8)+0x1f8 bit 8
    // The mouse cursor is shown (app+0x1f8 bit 4): only then do
    // mouse messages reach the menus (ShowMouse, the pause menu).
    bool cursor_shown = false;
    // A mouse message in stage coordinates, for the active menu (0x651cf4,
    // the original's).
    void mouse_event(int message, float x, float y);
    // The active FlashController (gm+0x678).
    menu::BaseMenu* active_controller() { return pause_shown_ ? pause_menu_.get() : controller_.get(); }
    // *(*(0x651ce8)+0x9c)+0x3c: the game is suspended (its fullscreen window
    // minimized): the movies still update but the state machine waits, so a
    // movie change doesn't go through.
    bool suspended = false;
    // A replay's change of `suspended`, taken at the start of the next tick
    // (the recording sees it after an update; the real game's state machine
    // saw it from the next tick). -1: none.
    int suspend_request = -1;

    // as::Host
    bool key_down(int code) override;
    void load_movie(MovieClip* target, as::NameId name) override;
    void unload_movie(MovieClip* target) override;
    void stop_all_sounds() override;  // StopSounds
    // A loaded movie's root clip was reset: one reference less on its movie.
    void release_loaded_movie(Player* movie);

    // The keyboard and pads, which the frontend fills in before each tick.
    input::Input input;

    int state() const { return state_; }
    // the game quits (state 7 ends the main loop).
    void quit() { state_ = 7; }
    bool quitting() const { return state_ == 7; }
    // A movie loaded by loadMovie, once ready (null before).
    Player* sub_movie(as::NameId name);
    // What a movie change or loadMovie is at, on one line (the GameCube logs
    // it while one is under way, to catch one that never finishes); empty
    // when none is.
    std::string loading_state() const;
    // Whether input reaches the game and what holds it (the GameCube logs it
    // when it changes, to catch a screen the game never leaves).
    std::string input_state() const;

    std::filesystem::path mod_dir;  // replacement graphics (player/mods.h), if any

    // Input replay: the real game's Key.isDown questions and answers, in call
    // order, from the capture hook's "!! key CODE RESULT" lines. While set,
    // key_down answers from it and counts questions that differ.
    bool replaying = false;
    // GetLocalGamerTag: the real game answers the Steam persona name
    //; a replay sets the captured one.
    std::string gamer_tag = "Player";
    // TESTING: the first level the game loads (any ../levelN/levelN.swf) is
    // level boot_level instead, once -- to reach a level without playing up
    // to it. 0: off. (PC: CASTLE_LEVEL; GameCube: CCGC/Scripts/Data/level.txt.)
    int boot_level = 0;
    bool boot_level_done = false;
    // TESTING: this character maxed in every save loaded (save::Storage::
    // max_out; 2 the red knight). 0: off. (PC: CASTLE_MAX; GameCube: max.txt.)
    int max_character = 0;
    // The save storage every local port reads, and the owned DLC packs
    // (g_nPDLC; bit 0 the Pink Knight Pack, bit 1 the Blacksmith Pack).
    save::Storage storage;
    // Level collision and waypoints (one set, like castle.exe's globals).
    Bsp bsp;
    std::filesystem::path bsp_path(const std::string& name) const { return swf_dir_.parent_path() / "bsp" / (name + ".pdag"); }
    uint32_t dlc = 3;
    std::vector<uint8_t> captured_storage;  // a replay's, to check ours against
    std::deque<std::pair<int, bool>> replay;
    int replay_mismatches = 0;

private:
    std::unique_ptr<Player> load(const std::string& name);
    void tick_sub_movie();
    std::string file_to_name(const std::string& file) const;

    std::filesystem::path swf_dir_;
    std::unique_ptr<Player> current_;
    std::unique_ptr<Player> loading_;
    std::unique_ptr<Player> pause_;
    std::unique_ptr<Player> incoming_;
    std::unique_ptr<menu::BaseMenu> controller_;  // gm+0x678 when it is a movie's own
    std::unique_ptr<menu::BaseMenu> pause_menu_;  // gm+0x67c, made at boot, kept
    bool pause_shown_ = false;    // gm+0x678 == gm+0x67c; the pause movie's node is active
    uint32_t pause_flags_ = 0;    // gm+0x48
    uint32_t paused_ports_ = 0;   // ctxs+0xb00
    // the original's, on every main-loop iteration: Start (or a lost pad or
    // window) opens the pause.
    void check_pause();
    std::unique_ptr<menu::BaseMenu> released_;    // freed next tick (it may still be running)
    std::map<as::NameId, int> globals_;  // gm+0x688
    int state_ = 0;  // 0 boot, 1 load movie, 2 run, 3 wait for the loading screen
    int boot_ticks_ = 0;
    bool sample_input_ = true;  // gm+0x6f0
    int input_off_ = 0;         // gm+0x6f4: why sampling is off (1 movie change, 2 loadMovie)
    bool loading_active_ = false;  // the loading screen's node is active
    struct SubMovie {
        as::NameId name;
        std::unique_ptr<Player> player;
        int refs = 0;
        bool used = true;  // false once unloaded: the slot's name is cleared
    };
    std::vector<SubMovie> subs_;  // gm slots 4-19
    // Unloaded movies, destroyed at the start of the next tick (the unload
    // can happen while one of their scripts is running).
    std::vector<std::unique_ptr<Player>> unloaded_;
    // GameManager::UnloadMovie (vtable +0x18): one reference less.
    void unload_sub_movie(Player* movie);
    int loading_sub_ = -1;        // gm+0x680
    int saved_state_ = 2;         // gm+0x20
    int incoming_updates_ = 0;
    std::string pending_;  // movie to change to (gm+0x69c)
};

}  // namespace player
